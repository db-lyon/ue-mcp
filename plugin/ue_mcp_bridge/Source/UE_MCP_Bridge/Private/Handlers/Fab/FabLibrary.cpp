#include "FabLibrary.h"

#include "HttpModule.h"
#include "GenericPlatform/GenericPlatformHttp.h"
#include "Interfaces/IHttpRequest.h"
#include "Interfaces/IHttpResponse.h"
#include "Misc/Base64.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "UObject/UnrealType.h"

#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
#include "DataStorage/Features.h"
#include "Elements/Columns/TypedElementWebColumns.h"
#include "Elements/Interfaces/TypedElementDataStorageInterface.h"
#include "Elements/Framework/TypedElementQueryBuilder.h"
#endif

namespace MCPFab
{
namespace LibraryPrivate
{
	FLibraryState Current;
	uint64 Generation = 0;
	FHttpRequestPtr ActiveRequest;

	bool IsAccountId(const FString& Value)
	{
		if (Value.Len() != 32) return false;
		for (TCHAR C : Value) if (!FChar::IsHexDigit(C)) return false;
		return true;
	}

	void RequestPage(FSession Session, int32 BatchSize, uint64 RequestGeneration)
	{
		auto Request = FHttpModule::Get().CreateRequest();
		ActiveRequest = Request;
		Request->SetURL(LibraryUrl(Session, BatchSize, Current.Next));
		Request->SetVerb(TEXT("GET"));
		Request->SetHeader(TEXT("Accept"), TEXT("application/json"));
		Request->SetHeader(TEXT("Authorization"), TEXT("Bearer ") + Session.Token);
		Request->SetTimeout(30.0f);
		Request->SetDelegateThreadPolicy(EHttpRequestDelegateThreadPolicy::CompleteOnGameThread);
		Request->OnProcessRequestComplete().BindLambda(
			[Session = MoveTemp(Session), BatchSize, RequestGeneration](FHttpRequestPtr, FHttpResponsePtr Response, bool bOk) mutable
		{
			if (RequestGeneration != Generation) return;
			ActiveRequest.Reset();
			if (!bOk || !Response.IsValid()) { Current.Fail(TEXT("Fab library transport failed. Retry sync_library.")); return; }
			if (!Current.Consume(Response->GetResponseCode(), Response->GetContentType(), Response->GetContentAsString())) return;
			if (!Current.Next.IsEmpty()) { RequestPage(MoveTemp(Session), BatchSize, RequestGeneration); return; }
			FString Error;
			if (!PublishLibrary(Current.Pending, Error)) { Current.Fail(Error); return; }
			Current.Items = MoveTemp(Current.Pending);
			Current.PublishedRows = Current.Items.Num();
			Current.State = TEXT("complete");
		});
		if (!Request->ProcessRequest()) Current.Fail(TEXT("Fab library request could not be started."));
	}
}

FString AccountFromToken(const FString& Token)
{
	TArray<FString> Parts;
	Token.ParseIntoArray(Parts, TEXT("."), false);
	if (Parts.Num() != 3) return {};
	FString Payload = Parts[1].Replace(TEXT("-"), TEXT("+")).Replace(TEXT("_"), TEXT("/"));
	while (Payload.Len() % 4) Payload += TEXT("=");
	FString Decoded;
	TSharedPtr<FJsonObject> Json;
	if (!FBase64::Decode(Payload, Decoded) || !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Decoded), Json) || !Json) return {};
	for (const TCHAR* Key : {TEXT("sub"), TEXT("account_id")})
	{
		FString Value;
		if (Json->TryGetStringField(Key, Value) && LibraryPrivate::IsAccountId(Value)) return Value;
	}
	return {};
}

#if WITH_DEV_AUTOMATION_TESTS
namespace LibraryPrivate
{
	TOptional<FSession> TestSession;
}

void SetSessionForTesting(const FSession* Session)
{
	if (Session) LibraryPrivate::TestSession = *Session;
	else LibraryPrivate::TestSession.Reset();
}
#endif

bool IsFetchableUrl(const FString& Url)
{
	if (Url.StartsWith(TEXT("https://"))) return true;
#if WITH_DEV_AUTOMATION_TESTS
	if (LibraryPrivate::TestSession.IsSet() && Url.StartsWith(TEXT("http://127.0.0.1:"))) return true;
#endif
	return false;
}

bool ReadSession(FSession& Out, FString& Error)
{
#if WITH_DEV_AUTOMATION_TESTS
	if (LibraryPrivate::TestSession.IsSet()) { Out = LibraryPrivate::TestSession.GetValue(); return true; }
#endif
	UClass* SettingsClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabSettings"));
	UClass* BrowserClass = FindObject<UClass>(nullptr, TEXT("/Script/Fab.FabBrowserApi"));
	if (!SettingsClass || !BrowserClass) { Error = TEXT("Fab plugin is not loaded."); return false; }
	UObject* Settings = SettingsClass->GetDefaultObject();
	// Read custom tokens directly: some engine getters log their value.
	if (const FStrProperty* Property = FindFProperty<FStrProperty>(SettingsClass, TEXT("CustomAuthToken")))
		Out.Token = Property->GetPropertyValue_InContainer(Settings);
	if (Out.Token.IsEmpty())
	{
		UObject* Browser = BrowserClass->GetDefaultObject();
		UFunction* Getter = Browser->FindFunction(TEXT("GetAuthToken"));
		if (!Getter) { Error = TEXT("Native Fab authentication API is unavailable."); return false; }
		struct FTokenResult { FString ReturnValue; } Result;
		Browser->ProcessEvent(Getter, &Result);
		Out.Token = MoveTemp(Result.ReturnValue);
	}
	Out.AccountId = AccountFromToken(Out.Token);
	if (Out.AccountId.IsEmpty()) { Out.Token.Reset(); Error = TEXT("No supported native Fab session. Sign in with fab(login) first."); return false; }
	int64 Environment = 0;
	if (const FEnumProperty* Property = FindFProperty<FEnumProperty>(SettingsClass, TEXT("Environment")))
		Environment = Property->GetUnderlyingProperty()->GetSignedIntPropertyValue(Property->ContainerPtrToValuePtr<void>(Settings));
	if (Environment == 0) Out.BaseUrl = TEXT("https://www.fab.com");
	else if (Environment == 1) Out.BaseUrl = TEXT("https://fab.cceb.dev.use1a.on.epicgames.com");
	else if (Environment == 2) Out.BaseUrl = TEXT("https://local-fab.on.epicgames.com");
	else { Error = TEXT("Unsupported Fab environment."); Out.Token.Reset(); return false; }
	return true;
}

FString LibraryUrl(const FSession& Session, int32 BatchSize, const FString& Cursor)
{
	FString Url = FString::Printf(TEXT("%s/e/accounts/%s/ue/library?count=%d"), *Session.BaseUrl, *Session.AccountId, BatchSize);
	if (!Cursor.IsEmpty()) Url += TEXT("&cursor=") + FGenericPlatformHttp::UrlEncode(Cursor);
	return Url;
}

void FLibraryState::Begin(const FString& Account)
{
	if (AccountId != Account) { Items.Reset(); PublishedRows = 0; }
	AccountId = Account;
	State = TEXT("fetching"); Error.Reset(); Next.Reset(); Pages = 0; HttpStatus = 0; Pending.Reset(); Cursors.Reset();
}

void FLibraryState::Fail(const FString& Message)
{
	State = TEXT("failed"); Error = Message; Pending.Reset(); Next.Reset();
}

bool FLibraryState::Consume(int32 Status, const FString& ContentType, const FString& Body)
{
	HttpStatus = Status;
	if (Status < 200 || Status >= 300) { Fail(FString::Printf(TEXT("Fab library returned HTTP %d. Previous snapshot retained."), Status)); return false; }
	TSharedPtr<FJsonObject> Json;
	const TArray<TSharedPtr<FJsonValue>>* Results = nullptr;
	if (!ContentType.StartsWith(TEXT("application/json"), ESearchCase::IgnoreCase)
		|| !FJsonSerializer::Deserialize(TJsonReaderFactory<>::Create(Body), Json) || !Json
		|| !Json->TryGetArrayField(TEXT("results"), Results))
	{ Fail(TEXT("Fab library returned an invalid JSON page. Previous snapshot retained.")); return false; }
	for (const auto& Item : *Results)
	{
		const TSharedPtr<FJsonObject>* Object = nullptr;
		FString Title, Asset, Namespace; FGuid Guid;
		if (!Item || !Item->TryGetObject(Object) || !(*Object)->TryGetStringField(TEXT("title"), Title)
			|| !(*Object)->TryGetStringField(TEXT("assetId"), Asset) || !FGuid::Parse(Asset, Guid)
			|| !(*Object)->TryGetStringField(TEXT("assetNamespace"), Namespace) || Namespace.IsEmpty())
		{ Fail(TEXT("Fab library returned a malformed item. Previous snapshot retained.")); return false; }
	}
	Next.Reset();
	const TSharedPtr<FJsonObject>* CursorObject = nullptr;
	if (Json->HasField(TEXT("cursors")))
	{
		if (!Json->TryGetObjectField(TEXT("cursors"), CursorObject)) { Fail(TEXT("Invalid library cursors object.")); return false; }
		const auto CursorValue = (*CursorObject)->TryGetField(TEXT("next"));
		if (CursorValue && CursorValue->Type != EJson::Null && !CursorValue->TryGetString(Next)) { Fail(TEXT("Invalid library next cursor.")); return false; }
	}
	if (!Next.IsEmpty() && (Cursors.Contains(Next) || Pages >= 10000))
	{ Fail(TEXT("Fab library pagination repeated a cursor or exceeded its page limit.")); return false; }
	if (!Next.IsEmpty()) Cursors.Add(Next);
	Pending.Append(*Results); ++Pages;
	State = Next.IsEmpty() ? TEXT("publishing") : TEXT("fetching");
	return true;
}

TSharedPtr<FJsonObject> FLibraryState::Describe() const
{
	auto Result = MakeShared<FJsonObject>();
	Result->SetStringField(TEXT("state"), State);
	Result->SetNumberField(TEXT("pages"), Pages);
	Result->SetNumberField(TEXT("fetchedItems"), Pending.Num());
	Result->SetNumberField(TEXT("snapshotItems"), Items.Num());
	Result->SetNumberField(TEXT("publishedRows"), PublishedRows);
	Result->SetNumberField(TEXT("httpStatus"), HttpStatus);
	if (!Error.IsEmpty()) Result->SetStringField(TEXT("error"), Error);
	return Result;
}

bool PublishLibrary(const TArray<TSharedPtr<FJsonValue>>& Items, FString& Error)
{
#if ENGINE_MAJOR_VERSION == 5 && ENGINE_MINOR_VERSION >= 8
	using namespace UE::Editor::DataStorage;
	auto* Storage = GetMutableDataStorageFeature<ICoreProvider>(StorageFeatureName);
	UScriptStruct* NameType = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Fab.FabObjectNameColumn"));
	UScriptStruct* ObjectType = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Fab.FabObjectColumn"));
	if (!Storage || !NameType || !ObjectType) { Error = TEXT("Fab Content Browser data storage is unavailable."); return false; }
	const TableHandle Table = Storage->FindTable(FName(TEXT("Fab")));
	if (Table == InvalidTableHandle) { Error = TEXT("Fab Content Browser table is not initialized."); return false; }
	// Validate the reflected schema before changing any existing rows.
	if (!FindFProperty<FNameProperty>(NameType, TEXT("Name")) || !FindFProperty<FStructProperty>(ObjectType, TEXT("AssetId")))
	{ Error = TEXT("Unsupported native Fab table schema."); return false; }
	TArray<RowHandle> PreviousRows;
	const auto Query = Storage->RegisterQuery(Queries::Select().Where().All(ObjectType).Compile());
	if (Query == InvalidQueryHandle) { Error = TEXT("Could not query the previous Fab snapshot."); return false; }
	Storage->RunQuery(Query, [&PreviousRows](const FQueryDescription&, IDirectQueryContext& Context) { PreviousRows.Append(Context.GetRowHandles()); });
	Storage->UnregisterQuery(Query);
	TArray<RowHandle> NewRows;
	int32 Index = 0; bool bValid = true;
	const bool bAdded = Items.IsEmpty() || Storage->BatchAddRow(Table, Items.Num(), [&](RowHandle Row)
	{
		NewRows.Add(Row);
		void* NameData = Storage->GetColumnData(Row, NameType);
		void* ObjectData = Storage->GetColumnData(Row, ObjectType);
		auto* Url = Storage->GetColumn<FEditorDataStorageUrlColumn>(Row);
		if (!NameData || !ObjectData || !Url) { bValid = false; ++Index; return; }
		const auto Json = Items[Index++]->AsObject();
		FindFProperty<FNameProperty>(NameType, TEXT("Name"))->SetPropertyValue_InContainer(NameData, FName(Json->GetStringField(TEXT("title"))));
		for (const auto& Pair : TArray<TPair<FName, FString>>{
			{TEXT("Description"), TEXT("description")}, {TEXT("Seller"), TEXT("seller")}})
		{
			FString Value;
			if (auto* P = FindFProperty<FStrProperty>(ObjectType, Pair.Key); P && Json->TryGetStringField(Pair.Value, Value)) P->SetPropertyValue_InContainer(ObjectData, Value);
		}
		for (const auto& Pair : TArray<TPair<FName, FString>>{
			{TEXT("ListingType"), TEXT("listingType")}, {TEXT("Source"), TEXT("source")}})
		{
			FString Value;
			if (auto* P = FindFProperty<FNameProperty>(ObjectType, Pair.Key); P && Json->TryGetStringField(Pair.Value, Value)) P->SetPropertyValue_InContainer(ObjectData, FName(Value));
		}
		for (const auto& Pair : TArray<TPair<FName, FString>>{
			{TEXT("AssetId"), TEXT("assetId")}, {TEXT("AssetNamespace"), TEXT("assetNamespace")}})
		{
			FGuid Value; FGuid::Parse(Json->GetStringField(Pair.Value), Value);
			if (auto* P = FindFProperty<FStructProperty>(ObjectType, Pair.Key)) *P->ContainerPtrToValuePtr<FGuid>(ObjectData) = Value;
		}
		Json->TryGetStringField(TEXT("url"), Url->UrlString);
		FString Distribution;
		if (Json->TryGetStringField(TEXT("distributionMethod"), Distribution))
		{
			if (auto* Tag = FindObject<UScriptStruct>(nullptr, TEXT("/Script/Fab.FabDistributionMethodTag")))
			{
				Distribution.ToLowerInline(); Distribution.ReplaceCharInline(TEXT('_'), TEXT(' '));
				const FDynamicColumnDescription Description{.TemplateType = Tag, .Identifier = FName(Distribution)};
				if (const auto* Type = Storage->GenerateDynamicColumn(Description)) Storage->AddColumn(Row, Type);
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Images = nullptr;
		if (Json->TryGetArrayField(TEXT("images"), Images)) for (const auto& Image : *Images)
		{
			if (!Image || Image->Type != EJson::Object) continue;
			auto ImageJson = Image->AsObject(); FString Type;
			FEditorDataStorageWebImageColumn Column;
			if (!ImageJson->TryGetStringField(TEXT("type"), Type) || !ImageJson->TryGetStringField(TEXT("url"), Column.UrlString)) continue;
			FString Width, Height;
			ImageJson->TryGetStringField(TEXT("width"), Width); ImageJson->TryGetStringField(TEXT("height"), Height);
			Column.Width = FMath::Clamp(FCString::Atoi(*Width), 0, 65535); Column.Height = FMath::Clamp(FCString::Atoi(*Height), 0, 65535);
			Storage->AddColumn(Row, FName(Type), MoveTemp(Column));
		}
	});
	if (!bAdded || !bValid || NewRows.Num() != Items.Num()) { Storage->BatchRemoveRows(NewRows); Error = TEXT("Fab table publication failed. Previous rows retained."); return false; }
	Storage->BatchRemoveRows(PreviousRows);
	return true;
#else
	Error = TEXT("Observable Fab library sync requires UE 5.8 or newer."); return false;
#endif
}

FLibraryState& Library() { return LibraryPrivate::Current; }

bool StartLibrarySync(int32 BatchSize, FString& Error)
{
	if (BatchSize < 1 || BatchSize > 1000) { Error = TEXT("batchSize must be between 1 and 1000."); return false; }
	if (Library().State == TEXT("fetching") || Library().State == TEXT("publishing")) { Error = TEXT("A library sync is already running. Poll fab(status)."); return false; }
	FSession Session;
	if (!ReadSession(Session, Error)) return false;
	Library().Begin(Session.AccountId);
	LibraryPrivate::RequestPage(MoveTemp(Session), BatchSize, ++LibraryPrivate::Generation);
	if (Library().State == TEXT("failed")) { Error = Library().Error; return false; }
	return true;
}

void ResetLibrary()
{
	++LibraryPrivate::Generation;
	if (LibraryPrivate::ActiveRequest)
	{
		LibraryPrivate::ActiveRequest->OnProcessRequestComplete().Unbind();
		LibraryPrivate::ActiveRequest->CancelRequest();
		LibraryPrivate::ActiveRequest.Reset();
	}
	LibraryPrivate::Current = FLibraryState();
}
}
