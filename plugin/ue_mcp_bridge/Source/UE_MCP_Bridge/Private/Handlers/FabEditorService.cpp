#include "FabEditorService.h"
#include "MCPHandlerRegistration.h"
#include "HandlerUtils.h"

#include "AssetRegistry/AssetRegistryModule.h"
#include "Containers/Ticker.h"
#include "Framework/Application/SlateApplication.h"
#include "HAL/PlatformTime.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "HttpModule.h"
#include "Interfaces/IHttpResponse.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/Base64.h"
#include "Misc/FileHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "SWebBrowser.h"
#include "UObject/StrongObjectPtr.h"
#include "UObject/StructOnScope.h"
#include "UObject/UObjectHash.h"
#include "UObject/UnrealType.h"
#include "Widgets/SWindow.h"

namespace MCPFabEditor
{
namespace Detail
{
	struct FOperation
	{
		FString Id = FGuid::NewGuid().ToString(EGuidFormats::Digits);
		FString Kind;
		FString State = TEXT("running");
		FString Error;
		FString Account;
		double Started = FPlatformTime::Seconds();
		double Deadline = Started + 30.0;
		int32 Pages = 0;
		int32 BatchSize = 500;
		TSet<FString> Cursors;
		TSet<FString> AssetIds;
		TArray<TSharedPtr<FJsonObject>> Items;
		TSharedPtr<FJsonObject> Result;
		FHttpRequestPtr Request;
		TWeakPtr<SWebBrowser> Browser;
		TStrongObjectPtr<UMCPFabBrowserReply> Reply;
	};
	static TMap<FString, TSharedPtr<FOperation>> Operations;
	static TArray<TSharedPtr<FJsonObject>> Library;
	static FString LibraryAccount;
	static FString LibraryRevision;
	static FString RefreshId;
	static bool LibraryUsable = false;
	static double LibraryFetchedAt = 0;
	static TArray<TWeakPtr<SWebBrowser>> Browsers;
	static FTSTicker::FDelegateHandle ExpiryTicker;

	static FString JsonString(const TSharedRef<FJsonObject>& Object)
	{
		FString Text;
		FJsonSerializer::Serialize(Object, TJsonWriterFactory<>::Create(&Text));
		return Text;
	}

	static void ReleaseBrowser(FOperation& Op)
	{
		if (auto Browser = Op.Browser.Pin(); Browser && Op.Reply.IsValid())
			Browser->UnbindUObject(TEXT("uemcpfab"), Op.Reply.Get(), false);
		Op.Reply.Reset();
	}

	static void Fail(FOperation& Op, const FString& Error)
	{
		Op.State = TEXT("failed");
		Op.Error = Error;
		Op.Request.Reset();
		ReleaseBrowser(Op);
	}

	static void ExpireOperations()
	{
		for (auto& Pair : Operations)
		{
			FOperation& Op = *Pair.Value;
			if (Op.State == TEXT("running") && FPlatformTime::Seconds() > Op.Deadline)
			{
				auto Request = Op.Request;
				Fail(Op, TEXT("Timed out. A submitted UI action may already have taken effect. Inspect Fab before retrying; sign-in or verification may need your attention."));
				if (Request) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); }
			}
		}
	}

	static TSharedPtr<FOperation> NewOperation(const FString& Kind)
	{
		ExpireOperations();
		if (Operations.Num() >= 64)
		{
			FString Oldest;
			double Time = TNumericLimits<double>::Max();
			for (const auto& Pair : Operations)
				if (Pair.Value->State != TEXT("running") && Pair.Value->Started < Time)
				{ Oldest = Pair.Key; Time = Pair.Value->Started; }
			if (Oldest.IsEmpty()) return nullptr;
			Operations.Remove(Oldest);
		}
		auto Op = MakeShared<FOperation>(); Op->Kind = Kind;
		Operations.Add(Op->Id, Op);
		return Op;
	}

	static TSharedPtr<FJsonValue> OperationResult(const FOperation& Op)
	{
		auto Res = MCPSuccess();
		Res->SetStringField(TEXT("operationId"), Op.Id);
		Res->SetStringField(TEXT("kind"), Op.Kind);
		Res->SetStringField(TEXT("state"), Op.State);
		Res->SetBoolField(TEXT("async"), Op.State == TEXT("running"));
		Res->SetBoolField(TEXT("outcomeUnknown"), Op.State == TEXT("failed") && Op.Kind != TEXT("refresh_library") && Op.Kind != TEXT("inspect") && Op.Error.StartsWith(TEXT("Timed out")));
		Res->SetNumberField(TEXT("pagesReceived"), Op.Pages);
		if (!Op.Error.IsEmpty()) { Res->SetBoolField(TEXT("success"), false); Res->SetStringField(TEXT("error"), Op.Error); }
		if (Op.Result) Res->SetObjectField(TEXT("result"), Op.Result);
		return MCPResult(Res);
	}

	static bool InvokeString(UObject* Object, const TCHAR* Name, const FString* Input, FString* Output)
	{
		if (!IsValid(Object)) return false;
		UFunction* Function = Object->FindFunction(FName(Name));
		if (!Function) return false;
		FStructOnScope Buffer(Function);
		FStrProperty* Return = nullptr;
		int32 Inputs = 0;
		for (TFieldIterator<FProperty> It(Function); It; ++It)
		{
			if (!It->HasAnyPropertyFlags(CPF_Parm)) continue;
			FStrProperty* String = CastField<FStrProperty>(*It);
			if (!String) return false;
			if (It->HasAnyPropertyFlags(CPF_ReturnParm)) Return = String;
			else { if (!Input || ++Inputs > 1) return false; String->SetPropertyValue_InContainer(Buffer.GetStructMemory(), *Input); }
		}
		if ((Input && Inputs != 1) || (Output && !Return)) return false;
		Object->ProcessEvent(Function, Buffer.GetStructMemory());
		if (Output) *Output = Return->GetPropertyValue_InContainer(Buffer.GetStructMemory());
		return true;
	}

	static UObject* FabApi()
	{
		UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
		if (!Class) return nullptr;
		TArray<UObject*> Objects;
		GetObjectsOfClass(Class, Objects, true, RF_ClassDefaultObject);
		for (UObject* Object : Objects) if (IsValid(Object)) return Object;
		return nullptr;
	}

	// Reuse the editor session internally. Never return/log the token or request
	// headers. Reject custom auth because Fab's custom-token getter logs it.
	static bool Auth(FString& Token, FString& Account, FString& Error)
	{
		UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabSettings"));
		UObject* Settings = SettingsClass ? SettingsClass->GetDefaultObject() : nullptr;
		if (!Settings) { Error = TEXT("Fab is not loaded. Enable Fab and open its editor window."); return false; }
		const auto* Custom = CastField<FStrProperty>(SettingsClass->FindPropertyByName(TEXT("CustomAuthToken")));
		const auto* Environment = CastField<FEnumProperty>(SettingsClass->FindPropertyByName(TEXT("Environment")));
		if (!Custom || !Environment || !Custom->GetPropertyValue_InContainer(Settings).IsEmpty() ||
			Environment->GetUnderlyingProperty()->GetSignedIntPropertyValue(Environment->ContainerPtrToValuePtr<void>(Settings)) != 0)
		{ Error = TEXT("Owned-library tools require Fab's production environment and normal editor sign-in."); return false; }
		UObject* Api = FabApi();
		if (!Api) { Error = TEXT("Open Fab in Unreal Editor and sign in before querying your library."); return false; }
		if (!InvokeString(Api, TEXT("GetAuthToken"), nullptr, &Token) || !AccountFromToken(Token, Account))
		{ Token.Reset(); Error = TEXT("Fab sign-in is unavailable or its token format is unsupported. Sign in again in the Fab editor window."); return false; }
		if (!LibraryAccount.IsEmpty() && Account != LibraryAccount) { LibraryUsable = false; Library.Reset(); LibraryAccount.Reset(); }
		return true;
	}

	static void FetchPage(const TSharedPtr<FOperation>& Op, const FString& Cursor)
	{
		FString Token, Account, Error;
		if (!Auth(Token, Account, Error) || Account != Op->Account)
		{ Fail(*Op, Error.IsEmpty() ? TEXT("Fab account changed during the refresh.") : Error); return; }
		if (Op->Pages >= 200 || Op->Items.Num() >= 100000)
		{ Fail(*Op, TEXT("Library safety limit reached; no partial ownership inventory was published.")); return; }
		FString Url = FString::Printf(TEXT("https://fab.com/e/accounts/%s/ue/library?count=%d"), *Account, Op->BatchSize);
		if (!Cursor.IsEmpty()) Url += TEXT("&cursor=") + FGenericPlatformHttp::UrlEncode(TEXT("\"") + Cursor + TEXT("\""));
		auto Request = FHttpModule::Get().CreateRequest();
		Op->Request = Request;
		Request->SetVerb(TEXT("GET")); Request->SetURL(Url);
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Token);
		Token.Reset(); Request->SetTimeout(25.0f);
		Request->OnProcessRequestComplete().BindLambda([Weak = TWeakPtr<FOperation>(Op)](FHttpRequestPtr, FHttpResponsePtr Response, bool Ok)
		{
			auto Current = Weak.Pin(); if (!Current || Current->State != TEXT("running")) return;
			if (!Ok || !Response || Response->GetResponseCode() != 200)
			{ Fail(*Current, FString::Printf(TEXT("Fab library request failed (HTTP %d). No owned results were published."), Response ? Response->GetResponseCode() : 0)); return; }
			if (Response->GetContent().Num() > 16 * 1024 * 1024)
			{ Fail(*Current, TEXT("Fab library response exceeds the safety limit.")); return; }
			TSharedPtr<FJsonObject> Page; FString Next, Error;
			TArray<TSharedPtr<FJsonObject>> Items;
			if (!FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Response->GetContentAsString()), Page) || !ParseLibraryPage(Page, Items, Next, Error))
			{ Fail(*Current, Error.IsEmpty() ? TEXT("Fab returned invalid library JSON.") : Error); return; }
			for (auto& Item : Items)
			{
				const FString Id = Item->GetStringField(TEXT("assetId"));
				if (!Current->AssetIds.Contains(Id)) { Current->AssetIds.Add(Id); Current->Items.Add(Item); }
			}
			++Current->Pages;
			if (!Next.IsEmpty())
			{
				if (Current->Cursors.Contains(Next)) { Fail(*Current, TEXT("Fab repeated a pagination cursor; refresh rejected.")); return; }
				Current->Cursors.Add(Next); FetchPage(Current, Next); return;
			}
			if (Current->Items.IsEmpty()) { Fail(*Current, TEXT("Fab returned an empty library; ownership could not be verified.")); return; }
			FString Token, Account, AuthError;
			if (!Auth(Token, Account, AuthError) || Account != Current->Account)
			{ Fail(*Current, TEXT("Fab account changed or signed out during refresh.")); return; }
			Library = MoveTemp(Current->Items); LibraryAccount = Account;
			LibraryRevision = Current->Id; LibraryUsable = true; LibraryFetchedAt = FPlatformTime::Seconds();
			Current->Result = MakeShared<FJsonObject>();
			Current->Result->SetNumberField(TEXT("count"), Library.Num());
			Current->Result->SetBoolField(TEXT("complete"), true);
			Current->Result->SetStringField(TEXT("source"), TEXT("authenticated_fab_ue_library"));
			Current->State = TEXT("completed"); Current->Request.Reset();
		});
		if (!Request->ProcessRequest()) Fail(*Op, TEXT("Fab library request could not start."));
	}

	static void FindBrowsers(const TSharedRef<SWidget>& Widget, int32 Depth, int32& Budget)
	{
		if (--Budget < 0 || Depth > 80) return;
		if (Widget->GetTypeAsString() == TEXT("SWebBrowser"))
		{
			auto Browser = StaticCastSharedRef<SWebBrowser>(Widget);
			if (IsFabUrl(Browser->GetUrl()))
			{
				bool Known = false;
				for (const auto& Existing : Browsers) if (Existing.Pin() == Browser) Known = true;
				if (!Known) Browsers.Add(Browser);
			}
		}
		FChildren* Children = Widget->GetChildren();
		if (Children) for (int32 I = 0; I < Children->Num(); ++I) FindBrowsers(Children->GetChildAt(I), Depth + 1, Budget);
	}

	static void DiscoverBrowsers()
	{
		if (!FSlateApplication::IsInitialized()) return;
		int32 Budget = 50000;
		for (const auto& Window : FSlateApplication::Get().GetTopLevelWindows()) FindBrowsers(Window, 0, Budget);
	}

	static TSharedPtr<FJsonObject> FindOwned(const FString& Id)
	{
		for (const auto& Item : Library) if (Item->GetStringField(TEXT("assetId")) == Id) return Item;
		return nullptr;
	}

	static FString ListingPath(FString Url)
	{
		if (!IsFabUrl(Url)) return TEXT("");
		int32 Cut;
		if (Url.FindChar('?', Cut)) Url.LeftInline(Cut);
		if (Url.FindChar('#', Cut)) Url.LeftInline(Cut);
		Url.RemoveFromEnd(TEXT("/"));
		const int32 At = Url.Find(TEXT("/listings/"));
		return At == INDEX_NONE ? FString() : Url.Mid(At);
	}

	static bool RequireLibrary(FString& Error)
	{
		FString Token, Account;
		if (!Auth(Token, Account, Error)) { LibraryUsable = false; return false; }
		if (!LibraryUsable || Account != LibraryAccount || FPlatformTime::Seconds() - LibraryFetchedAt > 1800)
		{ Error = TEXT("Refresh the owned library and wait for a successful complete operation first."); return false; }
		return true;
	}
}

bool IsFabUrl(const FString& Url)
{
	return Url.StartsWith(TEXT("https://fab.com/"), ESearchCase::IgnoreCase) ||
		Url.StartsWith(TEXT("https://www.fab.com/"), ESearchCase::IgnoreCase);
}

bool AccountFromToken(const FString& Token, FString& OutAccount)
{
	OutAccount.Reset(); FString Jwt = Token;
	if (Jwt.StartsWith(TEXT("eg1~"))) Jwt.RightChopInline(4);
	TArray<FString> Parts; Jwt.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 3 || Parts[1].Len() > 16384) return false;
	FString Payload = Parts[1].Replace(TEXT("-"), TEXT("+")).Replace(TEXT("_"), TEXT("/"));
	while (Payload.Len() % 4) Payload += TEXT("=");
	FString Decoded; TSharedPtr<FJsonObject> Object;
	if (!FBase64::Decode(Payload, Decoded) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Decoded), Object) || !Object) return false;
	if (!Object->TryGetStringField(TEXT("sub"), OutAccount) || OutAccount.Len() != 32) { OutAccount.Reset(); return false; }
	for (TCHAR Ch : OutAccount) if (!FChar::IsHexDigit(Ch)) { OutAccount.Reset(); return false; }
	return true; // Identity hint only; the authenticated library response verifies access.
}

bool ParseLibraryPage(const TSharedPtr<FJsonObject>& Page, TArray<TSharedPtr<FJsonObject>>& OutItems, FString& OutCursor, FString& OutError)
{
	OutItems.Reset(); OutCursor.Reset(); OutError.Reset();
	const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
	const TSharedPtr<FJsonObject>* Cursors = nullptr;
	if (!Page || !Page->TryGetArrayField(TEXT("results"), Results) || !Page->TryGetObjectField(TEXT("cursors"), Cursors))
	{ OutError = TEXT("Fab library response is missing results/cursors; no ownership was inferred."); return false; }
	const auto Next = (*Cursors)->TryGetField(TEXT("next"));
	if (Next && Next->Type != EJson::Null && (!Next->TryGetString(OutCursor) || OutCursor.Len() > 4096))
	{ OutError = TEXT("Fab returned an invalid next cursor."); return false; }
	for (const auto& Value : *Results)
	{
		const TSharedPtr<FJsonObject>* Source = nullptr; FString Id, Title;
		if (!Value || !Value->TryGetObject(Source) || !(*Source)->TryGetStringField(TEXT("assetId"), Id) ||
			Id.IsEmpty() || Id.Len() > 128 || !(*Source)->TryGetStringField(TEXT("title"), Title) || Title.IsEmpty())
		{ OutError = TEXT("Fab returned a malformed owned-library item."); OutItems.Reset(); return false; }
		auto Item = MakeShared<FJsonObject>();
		for (const TCHAR* Key : {TEXT("assetId"), TEXT("title"), TEXT("description"), TEXT("seller"), TEXT("listingType"), TEXT("assetNamespace"), TEXT("source"), TEXT("distributionMethod")})
		{ FString Text; if ((*Source)->TryGetStringField(Key, Text)) Item->SetStringField(Key, Text.Left(16000)); }
		FString Url;
		if ((*Source)->TryGetStringField(TEXT("url"), Url) && IsFabUrl(Url))
		{ int32 Cut; if (Url.FindChar('?', Cut)) Url.LeftInline(Cut); if (Url.FindChar('#', Cut)) Url.LeftInline(Cut); Item->SetStringField(TEXT("url"), Url); }
		const TArray<TSharedPtr<FJsonValue>>* Versions = nullptr;
		TArray<TSharedPtr<FJsonValue>> SafeVersions;
		if ((*Source)->TryGetArrayField(TEXT("projectVersions"), Versions)) for (const auto& Version : *Versions)
		{
			const TSharedPtr<FJsonObject>* Record = nullptr;
			if (!Version || !Version->TryGetObject(Record) || SafeVersions.Num() >= 100) continue;
			auto Safe = MakeShared<FJsonObject>();
			for (const TCHAR* Key : {TEXT("engineVersions"), TEXT("targetPlatforms"), TEXT("buildVersions")})
			{
				const TArray<TSharedPtr<FJsonValue>>* Values = nullptr; TArray<TSharedPtr<FJsonValue>> Strings;
				if ((*Record)->TryGetArrayField(Key, Values)) for (const auto& VersionValue : *Values)
				{ FString Text; if (VersionValue && VersionValue->TryGetString(Text) && Text.Len() <= 256 && Strings.Num() < 100) Strings.Add(MakeShared<FJsonValueString>(Text)); }
				Safe->SetArrayField(Key, Strings);
			}
			SafeVersions.Add(MakeShared<FJsonValueObject>(Safe));
		}
		Item->SetArrayField(TEXT("projectVersions"), SafeVersions);
		Item->SetBoolField(TEXT("owned"), true);
		Item->SetStringField(TEXT("ownershipSource"), TEXT("authenticated_fab_ue_library"));
		OutItems.Add(Item);
	}
	return true;
}

TSharedPtr<FJsonValue> Execute(const TSharedPtr<FJsonObject>& Params)
{
	using namespace Detail;
	check(IsInGameThread()); ExpireOperations();
	if (!Params) return MCPError(TEXT("Fab editor parameters must be an object."));
	for (const auto& Pair : Params->Values)
	{
		const FString Key(*Pair.Key);
		if (Key == TEXT("browserId") || Key == TEXT("batchSize") || Key == TEXT("limit") || Key == TEXT("offset"))
		{
			double Number = 0;
			if (!Pair.Value || !Pair.Value->TryGetNumber(Number) || !FMath::IsFinite(Number) || Number < 0 || Number > 1000000 || Number != FMath::FloorToDouble(Number))
				return MCPError(Key + TEXT(" must be a bounded nonnegative integer."));
		}
		else if (Key == TEXT("confirmDownload"))
		{ bool Value; if (!Pair.Value || !Pair.Value->TryGetBool(Value)) return MCPError(TEXT("confirmDownload must be boolean.")); }
		else if (Key == TEXT("operation") || Key == TEXT("operationId") || Key == TEXT("assetId") || Key == TEXT("query") || Key == TEXT("snapshotId") || Key == TEXT("elementId") || Key == TEXT("value"))
		{ FString Value; if (!Pair.Value || !Pair.Value->TryGetString(Value) || Value.Len() > 512) return MCPError(Key + TEXT(" must be a string of at most 512 characters.")); }
		else return MCPError(TEXT("Unknown Fab parameter: ") + Key);
	}
	const FString Action = OptionalString(Params, TEXT("operation"), TEXT("status"));
	if (Action == TEXT("operation_status") || Action == TEXT("cancel_operation"))
	{
		const auto* Found = Operations.Find(OptionalString(Params, TEXT("operationId")));
		if (!Found) return MCPError(TEXT("Unknown or expired Fab operationId."));
		auto Op = *Found;
		if (Action == TEXT("cancel_operation") && Op->State == TEXT("running"))
		{
			if (Op->Kind != TEXT("refresh_library") && Op->Kind != TEXT("inspect"))
				return MCPError(TEXT("A queued UI interaction cannot safely be recalled. Inspect Fab and check this operation instead; do not retry the click automatically."));
			Op->State = TEXT("cancelled"); ReleaseBrowser(*Op);
			if (Op->Request) { Op->Request->OnProcessRequestComplete().Unbind(); Op->Request->CancelRequest(); }
			Op->Request.Reset();
		}
		return OperationResult(*Op);
	}
	if (Action == TEXT("status"))
	{
		auto Res = MCPSuccess(); FString Token, Account, Error;
		const bool HasSession = Auth(Token, Account, Error);
		Res->SetBoolField(TEXT("sessionTokenAvailable"), HasSession);
		Res->SetStringField(TEXT("authenticationNote"), Error);
		Res->SetBoolField(TEXT("ownedLibraryReady"), HasSession && Account == LibraryAccount && LibraryUsable && FPlatformTime::Seconds() - LibraryFetchedAt <= 1800);
		Res->SetStringField(TEXT("refreshOperationId"), RefreshId);
		DiscoverBrowsers(); TArray<TSharedPtr<FJsonValue>> Tabs;
		for (int32 I = 0; I < Browsers.Num(); ++I) if (auto Browser = Browsers[I].Pin(); Browser && IsFabUrl(Browser->GetUrl()))
		{ auto Tab = MakeShared<FJsonObject>(); Tab->SetNumberField(TEXT("browserId"), I); Tab->SetBoolField(TEXT("loaded"), Browser->IsLoaded()); Tabs.Add(MakeShared<FJsonValueObject>(Tab)); }
		Res->SetArrayField(TEXT("browsers"), Tabs);
		return MCPResult(Res);
	}
	if (Action == TEXT("refresh_library"))
	{
		FString Token, Account, Error; if (!Auth(Token, Account, Error)) return MCPError(Error);
		if (const auto* Existing = Operations.Find(RefreshId); Existing && (*Existing)->State == TEXT("running")) return OperationResult(**Existing);
		const double Count = OptionalNumber(Params, TEXT("batchSize"), 500);
		if (!FMath::IsFinite(Count) || Count < 1 || Count > 1000 || Count != FMath::FloorToDouble(Count)) return MCPError(TEXT("batchSize must be an integer from 1 to 1000."));
		auto Op = NewOperation(Action); if (!Op) return MCPError(TEXT("Too many running Fab operations."));
		Op->Account = Account; Op->BatchSize = static_cast<int32>(Count); Op->Deadline = Op->Started + 300;
		RefreshId = Op->Id; LibraryUsable = false; FetchPage(Op, TEXT("")); return OperationResult(*Op);
	}
	if (Action == TEXT("search_library") || Action == TEXT("get_owned_asset"))
	{
		FString Error; if (!RequireLibrary(Error)) return MCPError(Error);
		const FString Id = OptionalString(Params, TEXT("assetId"));
		if (Action == TEXT("get_owned_asset") && Id.IsEmpty()) return MCPError(TEXT("assetId is required."));
		const int32 Limit = OptionalInt(Params, TEXT("limit"), 25), Offset = OptionalInt(Params, TEXT("offset"), 0);
		if (Limit < 1 || Limit > 100 || Offset < 0) return MCPError(TEXT("limit must be 1..100 and offset must be nonnegative."));
		TArray<FString> Terms; OptionalString(Params, TEXT("query")).ParseIntoArrayWS(Terms);
		TArray<TSharedPtr<FJsonValue>> Matches; int32 Total = 0;
		for (const auto& Item : Library)
		{
			if (!Id.IsEmpty() && Item->GetStringField(TEXT("assetId")) != Id) continue;
			FString Search;
			for (const TCHAR* Key : {TEXT("title"), TEXT("description"), TEXT("seller"), TEXT("listingType")}) { FString Text; Item->TryGetStringField(Key, Text); Search += Text + TEXT(" "); }
			bool Match = true; for (const FString& Term : Terms) if (!Search.Contains(Term)) Match = false;
			if (!Match) continue;
			if (Total++ >= Offset && Matches.Num() < Limit) Matches.Add(MakeShared<FJsonValueObject>(Item));
		}
		if (Action == TEXT("get_owned_asset") && Matches.IsEmpty()) return MCPError(TEXT("Asset is not in the verified owned library."));
		auto Res = MCPSuccess(); Res->SetArrayField(TEXT("items"), Matches); Res->SetNumberField(TEXT("totalMatches"), Total);
		Res->SetNumberField(TEXT("nextOffset"), Offset + Matches.Num()); Res->SetBoolField(TEXT("hasMore"), Offset + Matches.Num() < Total);
		Res->SetStringField(TEXT("revision"), LibraryRevision); Res->SetStringField(TEXT("source"), TEXT("authenticated_fab_ue_library")); return MCPResult(Res);
	}
	if (Action == TEXT("get_imported_assets"))
	{
		FString Error; if (!RequireLibrary(Error)) return MCPError(Error);
		const FString Id = OptionalString(Params, TEXT("assetId"));
		if (!FindOwned(Id)) return MCPError(TEXT("assetId must identify a verified owned listing."));
		UClass* LocalClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabLocalAssets"));
		FMapProperty* Map = LocalClass ? CastField<FMapProperty>(LocalClass->FindPropertyByName(TEXT("PathsListingID"))) : nullptr;
		FStrProperty* Key = Map ? CastField<FStrProperty>(Map->KeyProp) : nullptr;
		FStrProperty* Value = Map ? CastField<FStrProperty>(Map->ValueProp) : nullptr;
		if (!Key || !Value) return MCPError(TEXT("Fab's imported-folder mapping hook is unavailable in this engine."));
		FScriptMapHelper Helper(Map, Map->ContainerPtrToValuePtr<void>(LocalClass->GetDefaultObject()));
		FARFilter Filter; Filter.bRecursivePaths = true;
		TArray<TSharedPtr<FJsonValue>> Folders;
		for (int32 I = 0; I < Helper.GetMaxIndex(); ++I) if (Helper.IsValidIndex(I) && Value->GetPropertyValue(Helper.GetValuePtr(I)) == Id)
		{
			const FString Path = Key->GetPropertyValue(Helper.GetKeyPtr(I));
			if (Path.StartsWith(TEXT("/Game/"))) { Filter.PackagePaths.Add(FName(*Path)); Folders.Add(MakeShared<FJsonValueString>(Path)); }
		}
		TArray<FAssetData> Assets;
		if (!Filter.PackagePaths.IsEmpty()) FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry")).Get().GetAssets(Filter, Assets);
		Assets.Sort([](const FAssetData& A, const FAssetData& B) { return A.GetSoftObjectPath().ToString() < B.GetSoftObjectPath().ToString(); });
		TArray<TSharedPtr<FJsonValue>> Items;
		for (int32 I = 0; I < FMath::Min(Assets.Num(), 1000); ++I)
		{ auto Item = MakeShared<FJsonObject>(); Item->SetStringField(TEXT("assetPath"), Assets[I].GetSoftObjectPath().ToString()); Item->SetStringField(TEXT("class"), Assets[I].AssetClassPath.ToString()); Items.Add(MakeShared<FJsonValueObject>(Item)); }
		auto Res = MCPSuccess(); Res->SetArrayField(TEXT("folders"), Folders); Res->SetArrayField(TEXT("assets"), Items);
		Res->SetNumberField(TEXT("totalAssets"), Assets.Num()); Res->SetBoolField(TEXT("truncated"), Assets.Num() > 1000);
		Res->SetStringField(TEXT("note"), TEXT("Fab folder mapping plus Asset Registry readback; not a download-completion or asset-quality guarantee. Inspect actual meshes before PCG use.")); return MCPResult(Res);
	}
	if (Action == TEXT("open"))
	{
		FString Url = TEXT("https://fab.com/plugins/ue5");
		const FString Id = OptionalString(Params, TEXT("assetId"));
		if (!Id.IsEmpty())
		{
			FString Error; if (!RequireLibrary(Error)) return MCPError(Error);
			auto Item = FindOwned(Id); FString ListingUrl;
			if (!Item || !Item->TryGetStringField(TEXT("url"), ListingUrl)) return MCPError(TEXT("Owned asset has no verified Fab listing URL."));
			const FString Path = ListingPath(ListingUrl);
			if (Path.IsEmpty()) return MCPError(TEXT("Unsupported Fab listing URL."));
			Url += Path;
		}
		DiscoverBrowsers(); TSharedPtr<SWebBrowser> Existing;
		const int32 Selected = OptionalInt(Params, TEXT("browserId"), -1);
		if (Selected >= 0)
		{
			if (Browsers.IsValidIndex(Selected)) Existing = Browsers[Selected].Pin();
			if (!Existing || !IsFabUrl(Existing->GetUrl())) return MCPError(TEXT("browserId no longer identifies a Fab tab."));
		}
		else for (const auto& Candidate : Browsers) if (auto Tab = Candidate.Pin(); Tab && IsFabUrl(Tab->GetUrl()))
		{ if (Existing) return MCPError(TEXT("Multiple Fab tabs are open. Supply browserId from status.")); Existing = Tab; }
		if (Existing)
		{
			if (!Id.IsEmpty()) for (const auto& Pair : Operations)
				if (Pair.Value->State == TEXT("running") && Pair.Value->Browser.Pin() == Existing)
					return MCPError(TEXT("Wait for the current Fab browser operation before navigating this tab."));
			if (!Id.IsEmpty()) Existing->LoadURL(Url);
			auto Res = MCPSuccess(); Res->SetStringField(TEXT("state"), Id.IsEmpty() ? TEXT("open") : TEXT("opening")); return MCPResult(Res);
		}
		UClass* Class = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
		if (!Class || !InvokeString(Class->GetDefaultObject(), TEXT("OpenInNewTab"), &Url, nullptr)) return MCPError(TEXT("Fab's native OpenInNewTab hook is unavailable. Open Fab from the editor Window menu."));
		auto Res = MCPSuccess(); Res->SetStringField(TEXT("state"), TEXT("opening")); Res->SetStringField(TEXT("note"), TEXT("Use status then inspect after Fab loads.")); return MCPResult(Res);
	}
	if (Action != TEXT("inspect") && Action != TEXT("activate") && Action != TEXT("set_search") && Action != TEXT("select_option") && Action != TEXT("download"))
		return MCPError(TEXT("Unknown Fab editor operation."));
	DiscoverBrowsers(); TSharedPtr<SWebBrowser> Browser;
	const int32 BrowserId = OptionalInt(Params, TEXT("browserId"), -1);
	if (BrowserId >= 0) { if (Browsers.IsValidIndex(BrowserId)) Browser = Browsers[BrowserId].Pin(); }
	else for (const auto& Candidate : Browsers) if (auto Tab = Candidate.Pin(); Tab && IsFabUrl(Tab->GetUrl()))
	{ if (Browser) return MCPError(TEXT("Multiple Fab tabs are open. Supply browserId from status.")); Browser = Tab; }
	if (!Browser || !IsFabUrl(Browser->GetUrl()) || !Browser->IsLoaded()) return MCPError(TEXT("No loaded Fab browser matched. Open Fab and wait for it to load."));
	for (const auto& Pair : Operations) if (Pair.Value->State == TEXT("running") && Pair.Value->Browser.Pin() == Browser) return MCPError(TEXT("A Fab browser operation is already running on that tab."));
	if (Action == TEXT("download"))
	{
		if (!OptionalBool(Params, TEXT("confirmDownload"), false)) return MCPError(TEXT("download requires confirmDownload=true after explicit user authorization."));
		FString Error; if (!RequireLibrary(Error)) return MCPError(Error);
		auto Item = FindOwned(OptionalString(Params, TEXT("assetId"))); FString Url;
		if (!Item || !Item->TryGetStringField(TEXT("url"), Url)) return MCPError(TEXT("Download target is not a verified owned listing."));
		const FString Expected = ListingPath(Url);
		if (Expected.IsEmpty() || ListingPath(Browser->GetUrl()) != Expected) return MCPError(TEXT("Open the exact owned listing before submitting its download."));
	}
	const auto Plugin = IPluginManager::Get().FindPlugin(TEXT("UE_MCP_Bridge")); FString Script;
	if (!Plugin || !FFileHelper::LoadFileToString(Script, *(Plugin->GetBaseDir() / TEXT("Resources/FabEditor.js")))) return MCPError(TEXT("Fab editor script resource is missing from the plugin."));
	if (Action != TEXT("inspect") && (OptionalString(Params, TEXT("snapshotId")).IsEmpty() || OptionalString(Params, TEXT("elementId")).IsEmpty()))
		return MCPError(TEXT("Inspect first; snapshotId and elementId are required for an interaction."));
	if (OptionalString(Params, TEXT("value")).Len() > 512) return MCPError(TEXT("value exceeds 512 characters."));
	auto Op = NewOperation(Action); if (!Op) return MCPError(TEXT("Too many running Fab operations."));
	Op->Browser = Browser; Op->Reply.Reset(NewObject<UMCPFabBrowserReply>()); Op->Reply->OperationId = Op->Id;
	auto Args = MakeShared<FJsonObject>(); Args->SetStringField(TEXT("operation"), Action); Args->SetStringField(TEXT("nonce"), Op->Id);
	for (const TCHAR* Key : {TEXT("snapshotId"), TEXT("elementId"), TEXT("value")}) Args->SetStringField(Key, OptionalString(Params, Key));
	Browser->BindUObject(TEXT("uemcpfab"), Op->Reply.Get(), false);
	Browser->ExecuteJavascript(TEXT("(") + Script + TEXT(")(") + JsonString(Args) + TEXT(");"));
	return OperationResult(*Op);
}

void CompleteBrowser(const FString& OperationId, const FString& Nonce, const FString& Json)
{
	using namespace Detail;
	const auto* Found = Operations.Find(OperationId);
	if (!Found || Nonce != OperationId || (*Found)->State != TEXT("running")) return;
	auto Op = *Found;
	if (Json.Len() > 131072 || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Json), Op->Result) || !Op->Result)
	{ Fail(*Op, TEXT("Fab browser returned an invalid or oversized response.")); return; }
	bool Success = false;
	if (!Op->Result->TryGetBoolField(TEXT("success"), Success) || !Success)
	{ FString Error; Op->Result->TryGetStringField(TEXT("error"), Error); Fail(*Op, Error.Left(1000)); return; }
	Op->State = TEXT("completed"); ReleaseBrowser(*Op);
}

void Register()
{
	// The companion package owns the optional MCP tool surface. Register via
	// the extension API rather than adding an unadvertised built-in action.
	UEMCP::RegisterExternalHandler(TEXT("fab_editor"), &Execute);
	if (!Detail::ExpiryTicker.IsValid()) Detail::ExpiryTicker = FTSTicker::GetCoreTicker().AddTicker(
		FTickerDelegate::CreateLambda([](float) { Detail::ExpireOperations(); return true; }), 1.0f);
}

void Shutdown()
{
	UEMCP::UnregisterExternalHandler(TEXT("fab_editor"));
	if (Detail::ExpiryTicker.IsValid()) { FTSTicker::GetCoreTicker().RemoveTicker(Detail::ExpiryTicker); Detail::ExpiryTicker.Reset(); }
	for (auto& Pair : Detail::Operations)
	{
		Detail::ReleaseBrowser(*Pair.Value);
		if (auto Request = Pair.Value->Request) { Request->OnProcessRequestComplete().Unbind(); Request->CancelRequest(); }
	}
	Detail::Operations.Reset(); Detail::Library.Reset(); Detail::Browsers.Reset();
	Detail::LibraryUsable = false; Detail::LibraryAccount.Reset(); Detail::RefreshId.Reset();
}
}

void UMCPFabBrowserReply::Complete(const FString& Nonce, const FString& Json)
{
	MCPFabEditor::CompleteBrowser(OperationId, Nonce, Json);
}
