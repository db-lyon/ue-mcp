// level(create_outliner_folder) and level(delete_outliner_folder) (#1529).
//
// An outliner folder outlives the actors in it: emptying one with delete_actors
// leaves the folder behind, and nothing but Python could remove it. Each action
// is the other's rollback. Registration stays in LevelHandlers.cpp.

#include "LevelHandlers.h"

#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Editor.h"
#include "EditorActorFolders.h"
#include "EngineUtils.h"
#include "Folder.h"
#include "GameFramework/Actor.h"
#include "HandlerUtils.h"

namespace
{
	/** Outliner folder paths have no leading or trailing slash and use forward slashes. */
	FString NormalizeOutlinerFolderPath(const FString& In)
	{
		FString Path = In.TrimStartAndEnd().Replace(TEXT("\\"), TEXT("/"));
		while (Path.StartsWith(TEXT("/"))) Path.RightChopInline(1);
		while (Path.EndsWith(TEXT("/"))) Path.LeftChopInline(1);
		return Path;
	}

	/** folderPath and folderPaths[] together, normalized, without duplicates. */
	TArray<FString> ReadOutlinerFolderPaths(const TSharedPtr<FJsonObject>& Params, TArray<FString>& OutInvalid)
	{
		TArray<FString> Raw;
		const FString Single = OptionalString(Params, TEXT("folderPath"));
		if (HasParam(Params, TEXT("folderPath"))) Raw.Add(Single);
		const TArray<TSharedPtr<FJsonValue>>* Many = nullptr;
		if (TryGetArrayParam(Params, TEXT("folderPaths"), Many) && Many)
		{
			Raw.Append(JsonArrayToStringList(Many));
		}

		TArray<FString> Out;
		for (const FString& Entry : Raw)
		{
			const FString Path = NormalizeOutlinerFolderPath(Entry);
			if (Path.IsEmpty())
			{
				OutInvalid.Add(Entry);
				continue;
			}
			bool bDuplicate = false;
			for (const FString& Seen : Out)
			{
				if (Seen.Equals(Path, ESearchCase::IgnoreCase)) { bDuplicate = true; break; }
			}
			if (!bDuplicate) Out.Add(Path);
		}
		return Out;
	}

	TSharedPtr<FJsonObject> OutlinerFolderRow(const FString& Path, const TCHAR* Status)
	{
		TSharedPtr<FJsonObject> Row = MakeShared<FJsonObject>();
		Row->SetStringField(TEXT("folderPath"), Path);
		Row->SetStringField(TEXT("status"), Status);
		return Row;
	}

	TArray<TSharedPtr<FJsonValue>> OutlinerFolderStrings(const TArray<FString>& Values)
	{
		TArray<TSharedPtr<FJsonValue>> Out;
		for (const FString& Value : Values) Out.Add(MakeShared<FJsonValueString>(Value));
		return Out;
	}
}

TSharedPtr<FJsonValue> FLevelHandlers::CreateOutlinerFolder(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	TArray<FString> Invalid;
	const TArray<FString> Paths = ReadOutlinerFolderPaths(Params, Invalid);
	if (Paths.IsEmpty() && Invalid.IsEmpty())
	{
		return MCPError(TEXT("Pass folderPath or folderPaths: the World Outliner folders to create, such as Props/Lights"));
	}

	FActorFolders& Folders = FActorFolders::Get();
	const FFolder::FRootObject Root = FFolder::GetWorldRootFolder(World).GetRootObject();

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<FString> Created;
	int32 Existed = 0;
	for (const FString& Entry : Invalid)
	{
		TSharedPtr<FJsonObject> Row = OutlinerFolderRow(Entry, TEXT("failed"));
		Row->SetStringField(TEXT("reason"), TEXT("invalid_path"));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	for (const FString& Path : Paths)
	{
		const FFolder Folder(Root, FName(*Path));
		if (Folders.ContainsFolder(*World, Folder))
		{
			++Existed;
			Rows.Add(MakeShared<FJsonValueObject>(OutlinerFolderRow(Path, TEXT("existed"))));
			continue;
		}
		if (Folders.CreateFolder(*World, Folder) && Folders.ContainsFolder(*World, Folder))
		{
			Created.Add(Path);
			Rows.Add(MakeShared<FJsonValueObject>(OutlinerFolderRow(Path, TEXT("created"))));
		}
		else
		{
			TSharedPtr<FJsonObject> Row = OutlinerFolderRow(Path, TEXT("failed"));
			Row->SetStringField(TEXT("reason"), TEXT("create_failed"));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
		}
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	if (Created.Num() > 0) MCPSetCreated(Result); else MCPSetExisted(Result);
	Result->SetArrayField(TEXT("results"), Rows);
	Result->SetNumberField(TEXT("createdCount"), Created.Num());
	Result->SetNumberField(TEXT("existedCount"), Existed);
	Result->SetNumberField(TEXT("failedCount"), Rows.Num() - Created.Num() - Existed);
	if (Created.Num() > 0)
	{
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("folderPaths"), OutlinerFolderStrings(Created));
		MCPSetRollback(Result, TEXT("delete_outliner_folder"), Payload);
	}
	return MCPResult(Result);
}

TSharedPtr<FJsonValue> FLevelHandlers::DeleteOutlinerFolder(const TSharedPtr<FJsonObject>& Params)
{
	REQUIRE_EDITOR_WORLD(World);

	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), false);
	TArray<FString> Invalid;
	const TArray<FString> Paths = ReadOutlinerFolderPaths(Params, Invalid);
	if (Paths.IsEmpty() && Invalid.IsEmpty())
	{
		return MCPError(TEXT("Pass folderPath or folderPaths: the World Outliner folders to delete"));
	}

	FActorFolders& Folders = FActorFolders::Get();
	TArray<FFolder> AllFolders;
	Folders.ForEachFolder(*World, [&AllFolders](const FFolder& Folder)
	{
		AllFolders.Add(Folder);
		return true;
	});

	TArray<TSharedPtr<FJsonValue>> Rows;
	TArray<FString> Deleted;
	int32 FolderCount = 0;
	for (const FString& Entry : Invalid)
	{
		TSharedPtr<FJsonObject> Row = OutlinerFolderRow(Entry, TEXT("failed"));
		Row->SetStringField(TEXT("reason"), TEXT("invalid_path"));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}
	for (const FString& Path : Paths)
	{
		// The folder and every folder under it, the same subtree folderPathPrefix selects.
		TArray<FFolder> Subtree;
		for (const FFolder& Folder : AllFolders)
		{
			if (MCPFolderMatches(Folder.GetPath().ToString(), FString(), Path)) Subtree.Add(Folder);
		}

		TArray<FString> Occupants;
		int32 OccupantCount = 0;
		for (TActorIterator<AActor> It(World); It; ++It)
		{
			if (*It && !MCPActorFolderPath(*It).IsEmpty() && MCPActorInFolder(*It, FString(), Path))
			{
				if (Occupants.Num() < 10) Occupants.Add((*It)->GetActorLabel());
				++OccupantCount;
			}
		}

		if (Subtree.IsEmpty() && OccupantCount == 0)
		{
			Rows.Add(MakeShared<FJsonValueObject>(OutlinerFolderRow(Path, TEXT("absent"))));
			continue;
		}
		if (OccupantCount > 0)
		{
			TSharedPtr<FJsonObject> Row = OutlinerFolderRow(Path, TEXT("failed"));
			Row->SetStringField(TEXT("reason"), TEXT("not_empty"));
			Row->SetNumberField(TEXT("actorCount"), OccupantCount);
			Row->SetArrayField(TEXT("sampleActorLabels"), OutlinerFolderStrings(Occupants));
			Rows.Add(MakeShared<FJsonValueObject>(Row));
			continue;
		}

		// Deepest first, so no parent is removed while a child still names it.
		Subtree.Sort([](const FFolder& A, const FFolder& B)
		{
			return A.GetPath().ToString().Len() > B.GetPath().ToString().Len();
		});
		TArray<FString> Removed;
		for (const FFolder& Folder : Subtree)
		{
			if (!bDryRun) Folders.DeleteFolder(*World, Folder);
			if (bDryRun || !Folders.ContainsFolder(*World, Folder)) Removed.Add(Folder.GetPath().ToString());
		}
		FolderCount += Removed.Num();
		Deleted.Append(Removed);

		TSharedPtr<FJsonObject> Row = OutlinerFolderRow(Path,
			Removed.Num() == Subtree.Num() ? (bDryRun ? TEXT("would_delete") : TEXT("deleted")) : TEXT("failed"));
		if (Removed.Num() != Subtree.Num()) Row->SetStringField(TEXT("reason"), TEXT("delete_failed"));
		Row->SetArrayField(TEXT("folders"), OutlinerFolderStrings(Removed));
		Rows.Add(MakeShared<FJsonValueObject>(Row));
	}

	TSharedPtr<FJsonObject> Result = MCPSuccess();
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetArrayField(TEXT("results"), Rows);
	Result->SetNumberField(TEXT("deletedCount"), bDryRun ? 0 : FolderCount);
	Result->SetBoolField(TEXT("unchanged"), bDryRun || FolderCount == 0);
	Result->SetArrayField(TEXT("deletedFolders"), OutlinerFolderStrings(Deleted));
	if (!bDryRun && Deleted.Num() > 0)
	{
		// Shallowest first, so each parent exists before its children are made again.
		Deleted.Sort([](const FString& A, const FString& B) { return A.Len() < B.Len(); });
		TSharedPtr<FJsonObject> Payload = MakeShared<FJsonObject>();
		Payload->SetArrayField(TEXT("folderPaths"), OutlinerFolderStrings(Deleted));
		MCPSetRollback(Result, TEXT("create_outliner_folder"), Payload);
	}
	MCPNoteLoadedOnlyEnumeration(World, Result);
	return MCPResult(Result);
}
