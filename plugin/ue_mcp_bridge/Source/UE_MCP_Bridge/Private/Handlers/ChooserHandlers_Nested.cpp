// Nested-chooser object reference reading and remapping (#754).
//
// A ChooserTable that outputs objects stores its real references inside
// in-asset NestedChooser sub-tables: a column's rows EvaluateChooser into
// further UChooserTables whose own rows hold the leaf object paths.
// chooser(list_rows) rendered those as an opaque resultType:NestedChooser with
// an empty output, so there was no way to see - let alone repoint - the actual
// references.
//
// The reporter concluded this was impossible because Python's
// get_editor_property refused to read UChooserTable::ResultsStructs. That is a
// Python restriction (it enforces Blueprint visibility metadata), NOT a C++
// one: FProperty reflection ignores C++ access specifiers entirely, so the
// bridge can walk these structures directly. No engine fork required.
//
// Translation-unit partition of FChooserHandlers; registrations live in
// ChooserHandlers.cpp.

#include "ChooserHandlers.h"

#include "HandlerRegistry.h"
#include "HandlerUtils.h"

#include "UObject/StrongObjectPtr.h"

#include "Chooser.h"
#include "StructUtils/InstancedStruct.h"
#include "EditorAssetLibrary.h"
#include "ScopedTransaction.h"
#include "UObject/UnrealType.h"
#include "UObject/SoftObjectPtr.h"

namespace
{
	/** One object reference found somewhere inside a chooser's structures. */
	struct FChooserObjectRef
	{
		FString ChooserPath;   // which (possibly nested) table it lives in
		FString Location;      // e.g. "ResultsStructs[3].Asset"
		FString StructType;    // the FInstancedStruct's script struct name
		FString ObjectPath;    // current value
		FProperty* Property = nullptr;
		void* ValueAddr = nullptr;
		UObject* Owner = nullptr;
	};

	FString NormalizeObjectPath(const FString& In)
	{
		// "/Game/Foo/Bar.Bar" and "/Game/Foo/Bar" should compare equal, and the
		// editor writes some refs with a trailing _C or a subobject suffix.
		FString Path = In;
		Path.TrimStartAndEndInline();
		if (Path.StartsWith(TEXT("'")) && Path.EndsWith(TEXT("'")))
		{
			Path = Path.Mid(1, Path.Len() - 2);
		}
		// Strip a class prefix like Blueprint'/Game/...'
		int32 Quote = INDEX_NONE;
		if (Path.FindChar(TEXT('\''), Quote))
		{
			Path = Path.Mid(Quote + 1).Replace(TEXT("'"), TEXT(""));
		}
		FString Left, Right;
		if (Path.Split(TEXT("."), &Left, &Right, ESearchCase::CaseSensitive, ESearchDir::FromEnd))
		{
			// Only collapse "/Path/Name.Name" to "/Path/Name".
			FString Tail;
			if (Left.Split(TEXT("/"), nullptr, &Tail, ESearchCase::CaseSensitive, ESearchDir::FromEnd) && Tail == Right)
			{
				Path = Left;
			}
		}
		return Path;
	}

	/** Recursively collect object/soft-object references inside a struct value. */
	void CollectRefsInStruct(
		const UScriptStruct* Struct,
		void* StructMemory,
		UObject* Owner,
		const FString& ChooserPath,
		const FString& LocationPrefix,
		const FString& StructTypeName,
		TArray<FChooserObjectRef>& OutRefs,
		int32 Depth)
	{
		// Deep nesting is truncated rather than followed forever; the caller is
		// told via truncatedDepth on the response so partial is not read as complete.
		if (!Struct || !StructMemory || Depth > 6) return;

		for (TFieldIterator<FProperty> It(Struct); It; ++It)
		{
			FProperty* Prop = *It;
			if (!Prop) continue;
			void* Value = Prop->ContainerPtrToValuePtr<void>(StructMemory);
			const FString Location = LocationPrefix + TEXT(".") + Prop->GetName();

			if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Prop))
			{
				const FSoftObjectPtr& Ptr = SoftProp->GetPropertyValue(Value);
				FChooserObjectRef Ref;
				Ref.ChooserPath = ChooserPath;
				Ref.Location = Location;
				Ref.StructType = StructTypeName;
				Ref.ObjectPath = Ptr.ToString();
				Ref.Property = Prop;
				Ref.ValueAddr = Value;
				Ref.Owner = Owner;
				if (!Ref.ObjectPath.IsEmpty()) OutRefs.Add(Ref);
			}
			else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Prop))
			{
				UObject* Referenced = ObjProp->GetObjectPropertyValue(Value);
				// A nested UChooserTable is followed separately by the caller;
				// record everything else as a leaf reference.
				if (Referenced && !Referenced->IsA<UChooserTable>())
				{
					FChooserObjectRef Ref;
					Ref.ChooserPath = ChooserPath;
					Ref.Location = Location;
					Ref.StructType = StructTypeName;
					Ref.ObjectPath = Referenced->GetPathName();
					Ref.Property = Prop;
					Ref.ValueAddr = Value;
					Ref.Owner = Owner;
					OutRefs.Add(Ref);
				}
			}
			else if (FStructProperty* StructProp = CastField<FStructProperty>(Prop))
			{
				CollectRefsInStruct(StructProp->Struct, Value, Owner, ChooserPath, Location,
					StructTypeName, OutRefs, Depth + 1);
			}
			else if (FMapProperty* MapProp = CastField<FMapProperty>(Prop))
			{
				FScriptMapHelper Helper(MapProp, Value);
				for (FScriptMapHelper::FIterator MapIt = Helper.CreateIterator(); MapIt; ++MapIt)
				{
					if (FStructProperty* ValueStruct = CastField<FStructProperty>(MapProp->ValueProp))
					{
						CollectRefsInStruct(ValueStruct->Struct, Helper.GetValuePtr(MapIt), Owner,
							ChooserPath, FString::Printf(TEXT("%s[map:%d]"), *Location, MapIt.GetInternalIndex()), StructTypeName, OutRefs, Depth + 1);
					}
					else if (FObjectPropertyBase* ValueObj = CastField<FObjectPropertyBase>(MapProp->ValueProp))
					{
						UObject* Referenced = ValueObj->GetObjectPropertyValue(Helper.GetValuePtr(MapIt));
						if (Referenced && !Referenced->IsA<UChooserTable>())
						{
							FChooserObjectRef Ref;
							Ref.ChooserPath = ChooserPath;
							Ref.Location = FString::Printf(TEXT("%s[map:%d]"), *Location, MapIt.GetInternalIndex());
							Ref.StructType = StructTypeName;
							Ref.ObjectPath = Referenced->GetPathName();
							Ref.Property = MapProp->ValueProp;
							Ref.ValueAddr = Helper.GetValuePtr(MapIt);
							Ref.Owner = Owner;
							OutRefs.Add(Ref);
						}
					}
				}
			}
			else if (FArrayProperty* ArrayProp = CastField<FArrayProperty>(Prop))
			{
				FScriptArrayHelper Helper(ArrayProp, Value);
				for (int32 i = 0; i < Helper.Num(); ++i)
				{
					const FString Indexed = FString::Printf(TEXT("%s[%d]"), *Location, i);
					if (FStructProperty* InnerStruct = CastField<FStructProperty>(ArrayProp->Inner))
					{
						CollectRefsInStruct(InnerStruct->Struct, Helper.GetRawPtr(i), Owner,
							ChooserPath, Indexed, StructTypeName, OutRefs, Depth + 1);
					}
					else if (FSoftObjectProperty* InnerSoft = CastField<FSoftObjectProperty>(ArrayProp->Inner))
					{
						FChooserObjectRef Ref;
						Ref.ChooserPath = ChooserPath;
						Ref.Location = Indexed;
						Ref.StructType = StructTypeName;
						Ref.ObjectPath = InnerSoft->GetPropertyValue(Helper.GetRawPtr(i)).ToString();
						Ref.Property = ArrayProp->Inner;
						Ref.ValueAddr = Helper.GetRawPtr(i);
						Ref.Owner = Owner;
						if (!Ref.ObjectPath.IsEmpty()) OutRefs.Add(Ref);
					}
					else if (FObjectPropertyBase* InnerObj = CastField<FObjectPropertyBase>(ArrayProp->Inner))
					{
						UObject* Referenced = InnerObj->GetObjectPropertyValue(Helper.GetRawPtr(i));
						if (Referenced && !Referenced->IsA<UChooserTable>())
						{
							FChooserObjectRef Ref;
							Ref.ChooserPath = ChooserPath;
							Ref.Location = Indexed;
							Ref.StructType = StructTypeName;
							Ref.ObjectPath = Referenced->GetPathName();
							Ref.Property = ArrayProp->Inner;
							Ref.ValueAddr = Helper.GetRawPtr(i);
							Ref.Owner = Owner;
							OutRefs.Add(Ref);
						}
					}
				}
			}
		}
	}

	/** Every UChooserTable nested under a root, including the root itself. */
	void CollectNestedChoosers(UChooserTable* Root, TArray<UChooserTable*>& Out, int32 Depth = 0)
	{
		if (!Root || Depth > 8 || Out.Contains(Root)) return;
		Out.Add(Root);

#if WITH_EDITORONLY_DATA
		// Declared nested tables.
		for (const TObjectPtr<UChooserTable>& Nested : Root->NestedChoosers)
		{
			CollectNestedChoosers(Nested, Out, Depth + 1);
		}
#endif
		// Tables referenced from result structs (FNestedChooser/FEvaluateChooser
		// both hold a Chooser pointer), reached by reflection so the exact
		// struct type does not have to be known here.
		auto FollowStructArray = [&](TArray<FInstancedStruct>& Structs)
		{
			for (FInstancedStruct& Item : Structs)
			{
				const UScriptStruct* ScriptStruct = Item.GetScriptStruct();
				if (!ScriptStruct) continue;
				void* Memory = Item.GetMutableMemory();
				if (!Memory) continue;
				for (TFieldIterator<FProperty> It(ScriptStruct); It; ++It)
				{
					if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(*It))
					{
						UObject* Referenced = ObjProp->GetObjectPropertyValue(
							ObjProp->ContainerPtrToValuePtr<void>(Memory));
						if (UChooserTable* Table = Cast<UChooserTable>(Referenced))
						{
							CollectNestedChoosers(Table, Out, Depth + 1);
						}
					}
				}
			}
		};
#if WITH_EDITORONLY_DATA
		FollowStructArray(Root->ResultsStructs);
#endif
		FollowStructArray(Root->ColumnsStructs);
	}

	/** Gather every leaf object reference across a chooser and its nested tables. */
	void GatherAllRefs(UChooserTable* Root, TArray<FChooserObjectRef>& OutRefs)
	{
		TArray<UChooserTable*> Tables;
		CollectNestedChoosers(Root, Tables);

		for (UChooserTable* Table : Tables)
		{
			if (!Table) continue;
			const FString TablePath = Table->GetPathName();

			auto WalkArray = [&](TArray<FInstancedStruct>& Structs, const TCHAR* ArrayName)
			{
				for (int32 i = 0; i < Structs.Num(); ++i)
				{
					const UScriptStruct* ScriptStruct = Structs[i].GetScriptStruct();
					void* Memory = Structs[i].GetMutableMemory();
					if (!ScriptStruct || !Memory) continue;
					CollectRefsInStruct(ScriptStruct, Memory, Table, TablePath,
						FString::Printf(TEXT("%s[%d]"), ArrayName, i),
						ScriptStruct->GetName(), OutRefs, 0);
				}
			};
#if WITH_EDITORONLY_DATA
			WalkArray(Table->ResultsStructs, TEXT("ResultsStructs"));
#endif
			WalkArray(Table->ColumnsStructs, TEXT("ColumnsStructs"));
		}
	}

	TSharedPtr<FJsonObject> RefToJson(const FChooserObjectRef& Ref)
	{
		TSharedPtr<FJsonObject> Obj = MakeShared<FJsonObject>();
		Obj->SetStringField(TEXT("chooser"), Ref.ChooserPath);
		Obj->SetStringField(TEXT("location"), Ref.Location);
		Obj->SetStringField(TEXT("structType"), Ref.StructType);
		Obj->SetStringField(TEXT("objectPath"), Ref.ObjectPath);
		return Obj;
	}
}

// chooser(list_object_references): every leaf object reference reachable from a
// chooser, descending through nested tables.
TSharedPtr<FJsonValue> FChooserHandlers::ListObjectReferences(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UChooserTable* Chooser = Cast<UChooserTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Chooser)
	{
		return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *AssetPath));
	}

	const FString ClassFilter = OptionalString(Params, TEXT("classFilter"));
	const FString PathFilter = OptionalString(Params, TEXT("pathFilter"));

	TArray<FChooserObjectRef> Refs;
	GatherAllRefs(Chooser, Refs);

	TArray<UChooserTable*> Tables;
	CollectNestedChoosers(Chooser, Tables);

	TArray<TSharedPtr<FJsonValue>> Entries;
	for (const FChooserObjectRef& Ref : Refs)
	{
		if (!PathFilter.IsEmpty() && !Ref.ObjectPath.Contains(PathFilter, ESearchCase::IgnoreCase)) continue;
		if (!ClassFilter.IsEmpty())
		{
			// Resolve lazily: only load when the caller actually filters by class.
			UObject* Resolved = FindObject<UObject>(nullptr, *Ref.ObjectPath);
			if (!Resolved) Resolved = LoadObject<UObject>(nullptr, *Ref.ObjectPath);
			if (!Resolved || !Resolved->GetClass()->GetName().Contains(ClassFilter, ESearchCase::IgnoreCase))
			{
				continue;
			}
		}
		Entries.Add(MakeShared<FJsonValueObject>(RefToJson(Ref)));
	}

	TArray<TSharedPtr<FJsonValue>> TablePaths;
	for (UChooserTable* Table : Tables)
	{
		if (Table) TablePaths.Add(MakeShared<FJsonValueString>(Table->GetPathName()));
	}

	auto Result = MCPSuccess();
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetArrayField(TEXT("tables"), TablePaths);
	Result->SetNumberField(TEXT("tableCount"), TablePaths.Num());
	Result->SetArrayField(TEXT("references"), Entries);
	Result->SetNumberField(TEXT("referenceCount"), Entries.Num());
	Result->SetNumberField(TEXT("totalReferences"), Refs.Num());
	return MCPResult(Result);
}

// chooser(remap_object_references): repoint references matching `from` to `to`,
// across the whole nested structure. Dry-run by default.
TSharedPtr<FJsonValue> FChooserHandlers::RemapObjectReferences(const TSharedPtr<FJsonObject>& Params)
{
	FString AssetPath;
	if (auto Err = RequireStringAlt(Params, TEXT("assetPath"), TEXT("path"), AssetPath)) return Err;

	UChooserTable* Chooser = Cast<UChooserTable>(UEditorAssetLibrary::LoadAsset(AssetPath));
	if (!Chooser)
	{
		return MCPError(FString::Printf(TEXT("ChooserTable not found: %s"), *AssetPath));
	}

	// Either an exact from/to pair, or a prefix rewrite for the common
	// "adopt a vendor folder into our namespace" case.
	const FString From = OptionalString(Params, TEXT("from"));
	const FString To = OptionalString(Params, TEXT("to"));
	const FString FromPrefix = OptionalString(Params, TEXT("fromPrefix"));
	const FString ToPrefix = OptionalString(Params, TEXT("toPrefix"));
	const bool bExact = !From.IsEmpty() && !To.IsEmpty();
	const bool bPrefix = !FromPrefix.IsEmpty() && !ToPrefix.IsEmpty();
	if (bExact == bPrefix)
	{
		return MCPError(TEXT("Provide exactly one of: from+to (exact path swap), or fromPrefix+toPrefix (folder rewrite)"));
	}
	// Default to a dry run: this rewrites asset references in place.
	const bool bDryRun = OptionalBool(Params, TEXT("dryRun"), true);

	TArray<FChooserObjectRef> Refs;
	GatherAllRefs(Chooser, Refs);

	// ValueAddr points into TArray storage inside an FInstancedStruct, and any
	// load can run PostLoad and editor callbacks that resize those arrays,
	// leaving every later address dangling. So resolve/validate EVERY target in
	// a first pass (loads allowed), then re-walk to get fresh addresses and
	// write in a second pass with no loading at all.
	const FString NormFrom = NormalizeObjectPath(From);
	TArray<TSharedPtr<FJsonValue>> Changes;
	TSet<UObject*> Touched;
	int32 Rewritten = 0;
	// Rewriting references across a nested chooser tree must be undoable.
	TUniquePtr<FScopedTransaction> Transaction;
	if (!bDryRun)
	{
		Transaction = MakeUnique<FScopedTransaction>(FText::FromString(TEXT("MCP chooser remap object references")));
	}

	// Pass 1: decide and resolve. May load; must not write. Skipped entirely on
	// a dry run - a preview writes nothing, so loading every destination asset
	// to produce it is pure cost, and dryRun is the default.
	//
	// Targets are held strongly: a load here can compile a Blueprint, which
	// collects garbage, and nothing else references an asset loaded a moment
	// ago until pass 2 writes it.
	TMap<FString, TStrongObjectPtr<UObject>> ResolvedTargets;
	for (const FChooserObjectRef& Ref : Refs)
	{
		if (bDryRun) break;
		const FString Cur = NormalizeObjectPath(Ref.ObjectPath);
		FString Candidate;
		if (bExact)
		{
			if (Cur != NormFrom) continue;
			Candidate = To;
		}
		else
		{
			if (!Cur.StartsWith(FromPrefix, ESearchCase::IgnoreCase)) continue;
			const int32 At = Ref.ObjectPath.Find(FromPrefix, ESearchCase::IgnoreCase);
			if (At == INDEX_NONE) continue;
			Candidate = Ref.ObjectPath.Left(At) + ToPrefix + Ref.ObjectPath.RightChop(At + FromPrefix.Len());
		}
		if (ResolvedTargets.Contains(Candidate)) continue;
		UObject* Found = FindObject<UObject>(nullptr, *Candidate);
		if (!Found) Found = LoadObject<UObject>(nullptr, *Candidate);
		ResolvedTargets.Add(Candidate, TStrongObjectPtr<UObject>(Found));
	}

	// Pass 2: re-walk so ValueAddr is fresh after any loading above, then write.
	// A ref that only became visible because pass 1's loads resized the arrays
	// is reported as unresolved rather than written blind - see the miss branch.
	if (!bDryRun)
	{
		Refs.Reset();
		GatherAllRefs(Chooser, Refs);
	}

	for (FChooserObjectRef& Ref : Refs)
	{
		const FString Current = NormalizeObjectPath(Ref.ObjectPath);
		FString NewPath;
		if (bExact)
		{
			if (Current != NormFrom) continue;
			NewPath = To;
		}
		else
		{
			if (!Current.StartsWith(FromPrefix, ESearchCase::IgnoreCase)) continue;
			// Rewrite the ORIGINAL reference, not the normalized one. Normalize
			// collapses "/Game/Foo/Bar.Bar" to "/Game/Foo/Bar", and building the
			// replacement from that produced a package-only path: soft refs
			// became unresolvable and hard refs resolved to the UPackage.
			const FString Original = Ref.ObjectPath;
			const int32 PrefixAt = Original.Find(FromPrefix, ESearchCase::IgnoreCase);
			if (PrefixAt == INDEX_NONE) continue;
			NewPath = Original.Left(PrefixAt) + ToPrefix + Original.RightChop(PrefixAt + FromPrefix.Len());
		}

		TSharedPtr<FJsonObject> Change = RefToJson(Ref);
		Change->SetStringField(TEXT("newObjectPath"), NewPath);

		if (!bDryRun)
		{
			bool bOk = false;
			if (FSoftObjectProperty* SoftProp = CastField<FSoftObjectProperty>(Ref.Property))
			{
				// Validate before writing: a soft ref accepts any string, so an
				// unresolvable path would be stored silently and reported as a
				// successful rewrite.
				const FSoftObjectPath NewSoftPath(NewPath);
				if (!NewSoftPath.IsValid())
				{
					Change->SetStringField(TEXT("error"), FString::Printf(TEXT("Not a valid object path: %s"), *NewPath));
				}
				else if (!OptionalBool(Params, TEXT("allowMissing"), false) && !ResolvedTargets.FindRef(NewPath).IsValid())
				{
					Change->SetStringField(TEXT("error"), FString::Printf(
						TEXT("Target does not exist: %s (pass allowMissing=true to write it anyway)"), *NewPath));
				}
				else
				{
					if (Ref.Owner) Ref.Owner->Modify();
					SoftProp->SetPropertyValue(Ref.ValueAddr, FSoftObjectPtr(NewSoftPath));
					bOk = true;
				}
			}
			else if (FObjectPropertyBase* ObjProp = CastField<FObjectPropertyBase>(Ref.Property))
			{
				// Resolved in pass 1; no loading here, so ValueAddr stays valid.
				UObject* NewTarget = ResolvedTargets.FindRef(NewPath).Get();
				if (!NewTarget)
				{
					Change->SetStringField(TEXT("error"), FString::Printf(TEXT("Target not found: %s"), *NewPath));
				}
				else if (ObjProp->PropertyClass && !NewTarget->IsA(ObjProp->PropertyClass))
				{
					Change->SetStringField(TEXT("error"), FString::Printf(
						TEXT("%s is a %s, not a %s"), *NewPath,
						*NewTarget->GetClass()->GetName(), *ObjProp->PropertyClass->GetName()));
				}
				else
				{
					if (Ref.Owner) Ref.Owner->Modify();
					ObjProp->SetObjectPropertyValue(Ref.ValueAddr, NewTarget);
					bOk = true;
				}
			}
			Change->SetBoolField(TEXT("rewritten"), bOk);
			if (bOk)
			{
				++Rewritten;
				if (Ref.Owner) Touched.Add(Ref.Owner);
			}
		}
		Changes.Add(MakeShared<FJsonValueObject>(Change));
	}

	if (!bDryRun && Rewritten > 0)
	{
		// Recompile inside the transaction scope opened below by the caller's
		// undo buffer entry; without a transaction Modify() only dirties.
		for (UObject* Object : Touched)
		{
			if (Object) Object->MarkPackageDirty();
		}
		Chooser->Compile(/*bForce=*/true);
		Chooser->MarkPackageDirty();
	}

	auto Result = MCPSuccess();
	if (!bDryRun && Rewritten > 0) MCPSetUpdated(Result);
	Result->SetStringField(TEXT("assetPath"), AssetPath);
	Result->SetBoolField(TEXT("dryRun"), bDryRun);
	Result->SetNumberField(TEXT("matched"), Changes.Num());
	Result->SetNumberField(TEXT("rewritten"), Rewritten);
	Result->SetArrayField(TEXT("changes"), Changes);
	Result->SetStringField(TEXT("note"), bDryRun
		? TEXT("Dry run - nothing was written. Re-run with dryRun=false to apply.")
		: TEXT("References rewritten and the chooser recompiled. The asset is left dirty; save it when ready."));
	// The swapped pair is not the inverse of this call. Both modes match by
	// pattern rather than by location, so replaying with from and to exchanged
	// would also sweep up every reference that already pointed at the new
	// target before this ran: the rewrite is many-to-one and the reverse cannot
	// tell the two populations apart. Each entry in 'changes' carries the path
	// it replaced, but nothing in this surface writes one specific reference
	// back at its own location, which is what an inverse would need.
	MCPSetNoRollback(Result, (bDryRun || Rewritten == 0)
		? TEXT("Nothing was written, so there is nothing to undo.")
		: TEXT("Every matching reference across the nested chooser tree now points at the new target, and the paths "
			   "they replaced are reported per entry in 'changes'. Re-running with the pair swapped is not an "
			   "inverse: it would also drag back references that already pointed at the new target, and no bridge "
			   "call writes one reference back at its own location. The edit is inside an editor transaction, so "
			   "undo in the editor is the only way back."));
	return MCPResult(Result);
}
