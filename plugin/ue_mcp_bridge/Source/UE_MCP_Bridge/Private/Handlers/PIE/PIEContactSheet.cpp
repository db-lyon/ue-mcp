#include "Handlers/PIE/PIEContactSheet.h"
#include "IImageWrapper.h"
#include "IImageWrapperModule.h"
#include "Modules/ModuleManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

namespace MCPPIE
{
	namespace
	{
		// ── Minimal 5x7 bitmap font ──────────────────────────────────────
		// Each glyph is 7 rows; the low 5 bits of each byte are columns, bit 0x10
		// leftmost. Covers 0-9, uppercase A-Z, and a little punctuation. Labels are
		// uppercased before drawing; unsupported chars render as blank.
		struct FGlyph { uint8 Rows[7]; };

		const FGlyph* FindGlyph(TCHAR C)
		{
			auto G = [](uint8 a, uint8 b, uint8 c, uint8 d, uint8 e, uint8 f, uint8 g) -> FGlyph
			{
				return FGlyph{ { a, b, c, d, e, f, g } };
			};
			static const TMap<TCHAR, FGlyph> Font = {
				{ TEXT(' '), G(0x00,0x00,0x00,0x00,0x00,0x00,0x00) },
				{ TEXT('0'), G(0x0E,0x11,0x13,0x15,0x19,0x11,0x0E) },
				{ TEXT('1'), G(0x04,0x0C,0x04,0x04,0x04,0x04,0x0E) },
				{ TEXT('2'), G(0x0E,0x11,0x01,0x02,0x04,0x08,0x1F) },
				{ TEXT('3'), G(0x1F,0x02,0x04,0x02,0x01,0x11,0x0E) },
				{ TEXT('4'), G(0x02,0x06,0x0A,0x12,0x1F,0x02,0x02) },
				{ TEXT('5'), G(0x1F,0x10,0x1E,0x01,0x01,0x11,0x0E) },
				{ TEXT('6'), G(0x06,0x08,0x10,0x1E,0x11,0x11,0x0E) },
				{ TEXT('7'), G(0x1F,0x01,0x02,0x04,0x08,0x08,0x08) },
				{ TEXT('8'), G(0x0E,0x11,0x11,0x0E,0x11,0x11,0x0E) },
				{ TEXT('9'), G(0x0E,0x11,0x11,0x0F,0x01,0x02,0x0C) },
				{ TEXT('.'), G(0x00,0x00,0x00,0x00,0x00,0x0C,0x0C) },
				{ TEXT(':'), G(0x00,0x0C,0x0C,0x00,0x0C,0x0C,0x00) },
				{ TEXT('-'), G(0x00,0x00,0x00,0x1F,0x00,0x00,0x00) },
				{ TEXT('/'), G(0x01,0x02,0x02,0x04,0x08,0x08,0x10) },
				{ TEXT('#'), G(0x0A,0x0A,0x1F,0x0A,0x1F,0x0A,0x0A) },
				{ TEXT('%'), G(0x18,0x19,0x02,0x04,0x08,0x13,0x03) },
				{ TEXT('A'), G(0x0E,0x11,0x11,0x1F,0x11,0x11,0x11) },
				{ TEXT('B'), G(0x1E,0x11,0x11,0x1E,0x11,0x11,0x1E) },
				{ TEXT('C'), G(0x0E,0x11,0x10,0x10,0x10,0x11,0x0E) },
				{ TEXT('D'), G(0x1C,0x12,0x11,0x11,0x11,0x12,0x1C) },
				{ TEXT('E'), G(0x1F,0x10,0x10,0x1E,0x10,0x10,0x1F) },
				{ TEXT('F'), G(0x1F,0x10,0x10,0x1E,0x10,0x10,0x10) },
				{ TEXT('G'), G(0x0E,0x11,0x10,0x17,0x11,0x11,0x0F) },
				{ TEXT('H'), G(0x11,0x11,0x11,0x1F,0x11,0x11,0x11) },
				{ TEXT('I'), G(0x0E,0x04,0x04,0x04,0x04,0x04,0x0E) },
				{ TEXT('J'), G(0x07,0x02,0x02,0x02,0x02,0x12,0x0C) },
				{ TEXT('K'), G(0x11,0x12,0x14,0x18,0x14,0x12,0x11) },
				{ TEXT('L'), G(0x10,0x10,0x10,0x10,0x10,0x10,0x1F) },
				{ TEXT('M'), G(0x11,0x1B,0x15,0x15,0x11,0x11,0x11) },
				{ TEXT('N'), G(0x11,0x19,0x15,0x13,0x11,0x11,0x11) },
				{ TEXT('O'), G(0x0E,0x11,0x11,0x11,0x11,0x11,0x0E) },
				{ TEXT('P'), G(0x1E,0x11,0x11,0x1E,0x10,0x10,0x10) },
				{ TEXT('Q'), G(0x0E,0x11,0x11,0x11,0x15,0x12,0x0D) },
				{ TEXT('R'), G(0x1E,0x11,0x11,0x1E,0x14,0x12,0x11) },
				{ TEXT('S'), G(0x0F,0x10,0x10,0x0E,0x01,0x01,0x1E) },
				{ TEXT('T'), G(0x1F,0x04,0x04,0x04,0x04,0x04,0x04) },
				{ TEXT('U'), G(0x11,0x11,0x11,0x11,0x11,0x11,0x0E) },
				{ TEXT('V'), G(0x11,0x11,0x11,0x11,0x11,0x0A,0x04) },
				{ TEXT('W'), G(0x11,0x11,0x11,0x15,0x15,0x1B,0x11) },
				{ TEXT('X'), G(0x11,0x11,0x0A,0x04,0x0A,0x11,0x11) },
				{ TEXT('Y'), G(0x11,0x11,0x0A,0x04,0x04,0x04,0x04) },
				{ TEXT('Z'), G(0x1F,0x01,0x02,0x04,0x08,0x10,0x1F) },
			};
			return Font.Find(C);
		}

		// Load an image file into a BGRA FColor buffer. Detects format from bytes.
		bool LoadImageToColors(const FString& Path, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
		{
			TArray<uint8> Raw;
			if (!FFileHelper::LoadFileToArray(Raw, *Path)) return false;

			IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			const EImageFormat Fmt = IWM.DetectImageFormat(Raw.GetData(), Raw.Num());
			if (Fmt == EImageFormat::Invalid) return false;

			TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(Fmt);
			if (!Wrapper.IsValid() || !Wrapper->SetCompressed(Raw.GetData(), Raw.Num())) return false;

			TArray64<uint8> Rgba;
			if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, Rgba)) return false;

			OutW = Wrapper->GetWidth();
			OutH = Wrapper->GetHeight();
			if (OutW <= 0 || OutH <= 0) return false;

			OutPixels.SetNumUninitialized(OutW * OutH);
			FMemory::Memcpy(OutPixels.GetData(), Rgba.GetData(), FMath::Min<int64>(Rgba.Num(), static_cast<int64>(OutPixels.Num()) * sizeof(FColor)));
			return true;
		}

		void DrawText(TArray<FColor>& Dst, int32 DstW, int32 DstH, int32 X, int32 Y, const FString& Text, const FColor& Ink)
		{
			const FString Up = Text.ToUpper();
			int32 PenX = X;
			for (int32 i = 0; i < Up.Len(); ++i)
			{
				const FGlyph* Glyph = FindGlyph(Up[i]);
				if (Glyph)
				{
					for (int32 row = 0; row < 7; ++row)
					{
						const uint8 Bits = Glyph->Rows[row];
						for (int32 col = 0; col < 5; ++col)
						{
							if (Bits & (0x10 >> col))
							{
								const int32 px = PenX + col;
								const int32 py = Y + row;
								if (px >= 0 && px < DstW && py >= 0 && py < DstH)
								{
									Dst[py * DstW + px] = Ink;
								}
							}
						}
					}
				}
				PenX += 6; // 5px glyph + 1px space
			}
		}

		void BlitScaled(const TArray<FColor>& Src, int32 SW, int32 SH,
			TArray<FColor>& Dst, int32 DW, int32 DH, int32 DstX, int32 DstY, int32 CellW, int32 CellH)
		{
			// Nearest-neighbour scale of the source into a CellW x CellH region.
			for (int32 y = 0; y < CellH; ++y)
			{
				const int32 sy = (SH > 0) ? FMath::Min(SH - 1, (y * SH) / CellH) : 0;
				const int32 py = DstY + y;
				if (py < 0 || py >= DH) continue;
				for (int32 x = 0; x < CellW; ++x)
				{
					const int32 sx = (SW > 0) ? FMath::Min(SW - 1, (x * SW) / CellW) : 0;
					const int32 px = DstX + x;
					if (px < 0 || px >= DW) continue;
					Dst[py * DW + px] = Src[sy * SW + sx];
				}
			}
		}
	}

	bool ComposeContactSheet(
		const TArray<FString>& FramePaths,
		const FString& OutputPath,
		const FContactSheetParams& Params,
		const TArray<FString>* Labels)
	{
		if (FramePaths.Num() == 0) return false;

		// Sample evenly down to MaxCells.
		TArray<int32> Selected;
		const int32 Total = FramePaths.Num();
		const int32 Take = FMath::Clamp(Params.MaxCells, 1, Total);
		for (int32 i = 0; i < Take; ++i)
		{
			Selected.Add((Take == 1) ? 0 : (i * (Total - 1)) / (Take - 1));
		}

		// Load the first selected frame to derive cell aspect.
		TArray<FColor> First;
		int32 FW = 0, FH = 0;
		int32 FirstOk = INDEX_NONE;
		for (int32 s = 0; s < Selected.Num(); ++s)
		{
			if (LoadImageToColors(FramePaths[Selected[s]], First, FW, FH)) { FirstOk = s; break; }
		}
		if (FirstOk == INDEX_NONE) return false;

		const int32 CellW = FMath::Max(32, Params.CellWidth);
		const int32 CellH = FMath::Max(24, (CellW * FH) / FMath::Max(1, FW));
		const int32 Pad = 4;
		const int32 LabelH = Params.bLabels ? 10 : 0;

		const int32 Cols = (Params.Columns > 0)
			? Params.Columns
			: FMath::Max(1, FMath::CeilToInt(FMath::Sqrt(static_cast<float>(Selected.Num()))));
		const int32 Rows = FMath::DivideAndRoundUp(Selected.Num(), Cols);

		const int32 SheetW = Cols * (CellW + Pad) + Pad;
		const int32 SheetH = Rows * (CellH + LabelH + Pad) + Pad;

		TArray<FColor> Sheet;
		Sheet.Init(FColor(20, 20, 24, 255), SheetW * SheetH);

		const FColor Ink(235, 235, 235, 255);

		for (int32 idx = 0; idx < Selected.Num(); ++idx)
		{
			const int32 col = idx % Cols;
			const int32 row = idx / Cols;
			const int32 CellX = Pad + col * (CellW + Pad);
			const int32 CellY = Pad + row * (CellH + LabelH + Pad);

			TArray<FColor> Img;
			int32 IW = 0, IH = 0;
			const bool bLoaded = (idx == FirstOk)
				? (Img = First, IW = FW, IH = FH, true)
				: LoadImageToColors(FramePaths[Selected[idx]], Img, IW, IH);
			if (bLoaded && IW > 0 && IH > 0)
			{
				BlitScaled(Img, IW, IH, Sheet, SheetW, SheetH, CellX, CellY, CellW, CellH);
			}

			if (Params.bLabels)
			{
				FString Label;
				if (Labels && Selected[idx] < Labels->Num())
				{
					Label = (*Labels)[Selected[idx]];
				}
				else
				{
					Label = FString::Printf(TEXT("#%d %s"), Selected[idx],
						*FPaths::GetBaseFilename(FramePaths[Selected[idx]]));
				}
				DrawText(Sheet, SheetW, SheetH, CellX + 1, CellY + CellH + 1, Label, Ink);
			}
		}

		// Encode the sheet as JPEG.
		IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
		TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::JPEG);
		if (!Wrapper.IsValid() ||
			!Wrapper->SetRaw(Sheet.GetData(), static_cast<int64>(Sheet.Num()) * sizeof(FColor), SheetW, SheetH, ERGBFormat::BGRA, 8))
		{
			return false;
		}
		const TArray64<uint8>& Data = Wrapper->GetCompressed(FMath::Clamp(Params.Quality, 1, 100));
		if (Data.Num() == 0) return false;
		return FFileHelper::SaveArrayToFile(Data, *OutputPath);
	}
}
