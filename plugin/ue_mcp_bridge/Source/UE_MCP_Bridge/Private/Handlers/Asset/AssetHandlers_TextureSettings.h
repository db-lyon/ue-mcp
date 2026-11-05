#pragma once

#include "HandlerUtils.h"
#include "Engine/Texture2D.h"

namespace AssetTextureSettings
{
inline TSharedPtr<FJsonObject> Read(UTexture2D* Texture)
{
	auto Out = MakeShared<FJsonObject>();
	Out->SetStringField(TEXT("compressionSettings"), StaticEnum<TextureCompressionSettings>()->GetNameStringByValue(Texture->CompressionSettings).RightChop(3));
	Out->SetStringField(TEXT("lodGroup"), StaticEnum<TextureGroup>()->GetNameStringByValue(Texture->LODGroup).RightChop(13));
	Out->SetBoolField(TEXT("sRGB"), Texture->SRGB);
	Out->SetBoolField(TEXT("neverStream"), Texture->NeverStream);
	Out->SetBoolField(TEXT("flipGreenChannel"), Texture->bFlipGreenChannel);
	Out->SetNumberField(TEXT("maxTextureSize"), Texture->MaxTextureSize);
	return Out;
}

inline bool Parse(const TSharedPtr<FJsonObject>& Params, TSharedPtr<FJsonObject>& Out, FString& Error)
{
	Out = MakeShared<FJsonObject>();
	const TSharedPtr<FJsonObject>* Nested = nullptr;
	if (HasParam(Params, TEXT("settings")) && !TryGetObjectParam(Params, TEXT("settings"), Nested))
	{
		Error = TEXT("settings must be an object");
		return false;
	}
	if (Nested)
	{
		for (const auto& Pair : (*Nested)->Values)
		{
			const FString Key(*Pair.Key);
			if (Key != TEXT("compressionSettings") && Key != TEXT("lodGroup") && Key != TEXT("sRGB")
				&& Key != TEXT("neverStream") && Key != TEXT("flipGreenChannel") && Key != TEXT("maxTextureSize"))
			{
				Error = TEXT("Unknown texture setting: ") + Key;
				return false;
			}
		}
	}
	FString PresetValue;
	if (HasParam(Params, TEXT("preset")) && !TryGetStringParam(Params, TEXT("preset"), PresetValue))
	{
		Error = TEXT("preset must be normal, masks or color");
		return false;
	}
	const FString Preset = PresetValue.ToLower();
	if (!Preset.IsEmpty())
	{
		if (Preset != TEXT("normal") && Preset != TEXT("masks") && Preset != TEXT("color"))
		{
			Error = TEXT("preset must be normal, masks or color");
			return false;
		}
		Out->SetStringField(TEXT("compressionSettings"), Preset == TEXT("normal") ? TEXT("Normalmap") : Preset == TEXT("masks") ? TEXT("Masks") : TEXT("Default"));
		Out->SetBoolField(TEXT("sRGB"), Preset == TEXT("color"));
		Out->SetBoolField(TEXT("flipGreenChannel"), false);
	}
	for (const TCHAR* Key : { TEXT("compressionSettings"), TEXT("lodGroup"), TEXT("sRGB"), TEXT("neverStream"), TEXT("flipGreenChannel"), TEXT("maxTextureSize") })
	{
		if (Nested && (*Nested)->HasField(Key)) Out->SetField(Key, (*Nested)->Values[Key]);
		if (HasParam(Params, Key)) Out->SetField(Key, Params->Values[Key]);
	}
	for (const TCHAR* Key : { TEXT("sRGB"), TEXT("neverStream"), TEXT("flipGreenChannel") })
	{
		bool Value;
		if (Out->HasField(Key) && !Out->TryGetBoolField(Key, Value))
		{
			Error = FString::Printf(TEXT("%s must be boolean"), Key);
			return false;
		}
	}
	double Size;
	if (Out->HasField(TEXT("maxTextureSize")) && (!Out->TryGetNumberField(TEXT("maxTextureSize"), Size) || !FMath::IsFinite(Size) || Size < 0 || Size > 32768 || Size != FMath::FloorToDouble(Size)))
	{
		Error = TEXT("maxTextureSize must be an integer from 0 to 32768 (0 uses the source size)");
		return false;
	}
	for (const TCHAR* Key : { TEXT("compressionSettings"), TEXT("lodGroup") })
	{
		if (!Out->HasField(Key)) continue;
		FString Name;
		const bool bCompression = FCString::Strcmp(Key, TEXT("compressionSettings")) == 0;
		const FString Prefix = bCompression ? TEXT("TC_") : TEXT("TEXTUREGROUP_");
		UEnum* Enum = bCompression ? StaticEnum<TextureCompressionSettings>() : StaticEnum<TextureGroup>();
		if (!Out->TryGetStringField(Key, Name)) { Error = FString(Key) + TEXT(" must be a string"); return false; }
		if (!Name.StartsWith(Prefix)) Name = Prefix + Name;
		const int64 Value = Enum->GetValueByNameString(Name);
		if (Value == INDEX_NONE || Value >= Enum->GetMaxEnumValue())
		{
			Error = FString::Printf(TEXT("Unknown %s: %s"), Key, *Name);
			return false;
		}
		Out->SetStringField(Key, Enum->GetNameStringByValue(Value).RightChop(Prefix.Len()));
	}
	return true;
}

inline void Apply(UTexture2D* Texture, const TSharedPtr<FJsonObject>& Settings)
{
	FString Name;
	bool Value;
	double Size;
	if (Settings->TryGetStringField(TEXT("compressionSettings"), Name)) Texture->CompressionSettings = static_cast<TextureCompressionSettings>(StaticEnum<TextureCompressionSettings>()->GetValueByNameString(TEXT("TC_") + Name));
	if (Settings->TryGetStringField(TEXT("lodGroup"), Name)) Texture->LODGroup = static_cast<TextureGroup>(StaticEnum<TextureGroup>()->GetValueByNameString(TEXT("TEXTUREGROUP_") + Name));
	if (Settings->TryGetBoolField(TEXT("sRGB"), Value)) Texture->SRGB = Value;
	if (Settings->TryGetBoolField(TEXT("neverStream"), Value)) Texture->NeverStream = Value;
	if (Settings->TryGetBoolField(TEXT("flipGreenChannel"), Value)) Texture->bFlipGreenChannel = Value;
	if (Settings->TryGetNumberField(TEXT("maxTextureSize"), Size)) Texture->MaxTextureSize = static_cast<int32>(Size);
}
}
