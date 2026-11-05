#include "Handlers/PIE/PIEGifEncoder.h"
#include "Handlers/PIE/PIELog.h"
#include "ImageUtils.h"
#include "Misc/FileHelper.h"
#include "IImageWrapperModule.h"
#include "IImageWrapper.h"

namespace MCPPIE
{
	namespace
	{
		// 6x7x6 uniform palette = 252 colors + 4 unused slots
		static constexpr int32 RLevels = 6;
		static constexpr int32 GLevels = 7;
		static constexpr int32 BLevels = 6;
		static constexpr int32 PaletteSize = 256;

		uint8 Quantize(uint8 Val, int32 Levels)
		{
			return static_cast<uint8>(FMath::Min(Val * Levels / 256, Levels - 1));
		}

		uint8 Dequantize(uint8 Qval, int32 Levels)
		{
			return static_cast<uint8>(Qval * 255 / (Levels - 1));
		}

		uint8 PaletteIndex(uint8 R, uint8 G, uint8 B)
		{
			return Quantize(R, RLevels) * (GLevels * BLevels)
				+ Quantize(G, GLevels) * BLevels
				+ Quantize(B, BLevels);
		}

		void BuildGlobalPalette(TArray<uint8>& Out)
		{
			Out.SetNumZeroed(PaletteSize * 3);
			for (int32 r = 0; r < RLevels; r++)
			{
				for (int32 g = 0; g < GLevels; g++)
				{
					for (int32 b = 0; b < BLevels; b++)
					{
						int32 Idx = (r * GLevels * BLevels + g * BLevels + b) * 3;
						Out[Idx + 0] = Dequantize(r, RLevels);
						Out[Idx + 1] = Dequantize(g, GLevels);
						Out[Idx + 2] = Dequantize(b, BLevels);
					}
				}
			}
		}

		void QuantizeFrame(const TArray<FColor>& Pixels, int32 W, int32 H,
			int32 TargetW, int32 TargetH, TArray<uint8>& OutIndices)
		{
			OutIndices.SetNum(TargetW * TargetH);
			const float ScaleX = static_cast<float>(W) / TargetW;
			const float ScaleY = static_cast<float>(H) / TargetH;

			for (int32 y = 0; y < TargetH; y++)
			{
				const int32 SrcY = FMath::Min(static_cast<int32>(y * ScaleY), H - 1);
				for (int32 x = 0; x < TargetW; x++)
				{
					const int32 SrcX = FMath::Min(static_cast<int32>(x * ScaleX), W - 1);
					const FColor& C = Pixels[SrcY * W + SrcX];
					OutIndices[y * TargetW + x] = PaletteIndex(C.R, C.G, C.B);
				}
			}
		}

		struct FLzwEncoder
		{
			TArray<uint8> Output;
			int32 MinCodeSize;
			int32 ClearCode;
			int32 EoiCode;
			int32 NextCode;
			int32 CodeSize;

			struct FEntry { int32 Prefix; uint8 Suffix; };
			TMap<int64, int32> Table;

			uint32 BitBuf = 0;
			int32 BitCount = 0;
			TArray<uint8> SubBlock;

			void Init(int32 InMinCodeSize)
			{
				MinCodeSize = InMinCodeSize;
				ClearCode = 1 << MinCodeSize;
				EoiCode = ClearCode + 1;
				Reset();
			}

			void Reset()
			{
				Table.Reset();
				NextCode = EoiCode + 1;
				CodeSize = MinCodeSize + 1;
			}

			int64 MakeKey(int32 Prefix, uint8 Suffix)
			{
				return (static_cast<int64>(Prefix) << 8) | Suffix;
			}

			void EmitCode(int32 Code)
			{
				BitBuf |= (static_cast<uint32>(Code) << BitCount);
				BitCount += CodeSize;
				while (BitCount >= 8)
				{
					SubBlock.Add(static_cast<uint8>(BitBuf & 0xFF));
					BitBuf >>= 8;
					BitCount -= 8;
					if (SubBlock.Num() == 255)
					{
						FlushSubBlock();
					}
				}
			}

			void FlushSubBlock()
			{
				if (SubBlock.Num() > 0)
				{
					Output.Add(static_cast<uint8>(SubBlock.Num()));
					Output.Append(SubBlock);
					SubBlock.Reset();
				}
			}

			void Encode(const TArray<uint8>& Indices)
			{
				Output.Add(static_cast<uint8>(MinCodeSize));

				EmitCode(ClearCode);

				if (Indices.Num() == 0)
				{
					EmitCode(EoiCode);
					if (BitCount > 0)
					{
						SubBlock.Add(static_cast<uint8>(BitBuf & 0xFF));
					}
					FlushSubBlock();
					Output.Add(0);
					return;
				}

				int32 Cur = Indices[0];
				for (int32 i = 1; i < Indices.Num(); i++)
				{
					uint8 Next = Indices[i];
					int64 Key = MakeKey(Cur, Next);
					if (int32* Found = Table.Find(Key))
					{
						Cur = *Found;
					}
					else
					{
						EmitCode(Cur);
						if (NextCode < 4096)
						{
							Table.Add(Key, NextCode++);
							if (NextCode > (1 << CodeSize) && CodeSize < 12)
							{
								CodeSize++;
							}
						}
						else
						{
							EmitCode(ClearCode);
							Reset();
						}
						Cur = Next;
					}
				}

				EmitCode(Cur);
				EmitCode(EoiCode);

				if (BitCount > 0)
				{
					SubBlock.Add(static_cast<uint8>(BitBuf & 0xFF));
				}
				FlushSubBlock();
				Output.Add(0); // block terminator
			}
		};

		void WriteU16(TArray<uint8>& Out, uint16 V)
		{
			Out.Add(V & 0xFF);
			Out.Add((V >> 8) & 0xFF);
		}

		bool LoadPngPixels(const FString& Path, TArray<FColor>& OutPixels, int32& OutW, int32& OutH)
		{
			TArray<uint8> FileData;
			if (!FFileHelper::LoadFileToArray(FileData, *Path)) return false;

			IImageWrapperModule& IWM = FModuleManager::LoadModuleChecked<IImageWrapperModule>(TEXT("ImageWrapper"));
			TSharedPtr<IImageWrapper> Wrapper = IWM.CreateImageWrapper(EImageFormat::PNG);
			if (!Wrapper.IsValid()) return false;
			if (!Wrapper->SetCompressed(FileData.GetData(), FileData.Num())) return false;

			OutW = Wrapper->GetWidth();
			OutH = Wrapper->GetHeight();

			TArray<uint8> RawData;
			if (!Wrapper->GetRaw(ERGBFormat::BGRA, 8, RawData)) return false;

			OutPixels.SetNum(OutW * OutH);
			FMemory::Memcpy(OutPixels.GetData(), RawData.GetData(), OutW * OutH * 4);
			return true;
		}
	}

	bool EncodeAnimatedGif(
		const TArray<FString>& FramePaths,
		const FString& OutputPath,
		const FGifEncodeParams& Params)
	{
		if (FramePaths.Num() == 0) return false;

		TArray<FColor> FirstPixels;
		int32 SrcW = 0, SrcH = 0;
		if (!LoadPngPixels(FramePaths[0], FirstPixels, SrcW, SrcH))
		{
			UE_LOG(LogMCPPIE, Warning, TEXT("[GIF] Failed to load first frame: %s"), *FramePaths[0]);
			return false;
		}

		int32 GifW = SrcW;
		int32 GifH = SrcH;
		if (Params.MaxWidth > 0 && GifW > Params.MaxWidth)
		{
			GifH = FMath::Max(1, GifH * Params.MaxWidth / GifW);
			GifW = Params.MaxWidth;
		}

		TArray<uint8> Palette;
		BuildGlobalPalette(Palette);

		TArray<uint8> Gif;
		Gif.Reserve(1024 * 1024);

		// Header
		Gif.Append(reinterpret_cast<const uint8*>("GIF89a"), 6);

		// Logical Screen Descriptor
		WriteU16(Gif, static_cast<uint16>(GifW));
		WriteU16(Gif, static_cast<uint16>(GifH));
		Gif.Add(0xF7); // GCT flag, 8 bits/color, 256 entries (2^(7+1))
		Gif.Add(0);     // background color index
		Gif.Add(0);     // pixel aspect ratio

		// Global Color Table
		Gif.Append(Palette);

		// NETSCAPE2.0 Application Extension (looping)
		Gif.Add(0x21); // extension
		Gif.Add(0xFF); // application
		Gif.Add(0x0B); // block size
		Gif.Append(reinterpret_cast<const uint8*>("NETSCAPE2.0"), 11);
		Gif.Add(0x03); // sub-block size
		Gif.Add(0x01); // loop sub-block id
		WriteU16(Gif, static_cast<uint16>(Params.LoopCount));
		Gif.Add(0x00); // terminator

		auto WriteFrame = [&](const TArray<uint8>& Indices)
		{
			// Graphics Control Extension
			Gif.Add(0x21); // extension
			Gif.Add(0xF9); // GCE
			Gif.Add(0x04); // block size
			Gif.Add(0x00); // no transparency, dispose = none
			WriteU16(Gif, static_cast<uint16>(Params.DelayCs));
			Gif.Add(0x00); // transparent color index (unused)
			Gif.Add(0x00); // terminator

			// Image Descriptor
			Gif.Add(0x2C); // image separator
			WriteU16(Gif, 0); // left
			WriteU16(Gif, 0); // top
			WriteU16(Gif, static_cast<uint16>(GifW));
			WriteU16(Gif, static_cast<uint16>(GifH));
			Gif.Add(0x00); // no local color table

			// LZW data
			FLzwEncoder Lzw;
			Lzw.Init(8); // min code size = 8 for 256 colors
			Lzw.Encode(Indices);
			Gif.Append(Lzw.Output);
		};

		// First frame (already loaded)
		{
			TArray<uint8> Indices;
			QuantizeFrame(FirstPixels, SrcW, SrcH, GifW, GifH, Indices);
			WriteFrame(Indices);
		}

		// Remaining frames
		for (int32 i = 1; i < FramePaths.Num(); i++)
		{
			TArray<FColor> Pixels;
			int32 W = 0, H = 0;
			if (!LoadPngPixels(FramePaths[i], Pixels, W, H))
			{
				UE_LOG(LogMCPPIE, Warning, TEXT("[GIF] Skipping unreadable frame: %s"), *FramePaths[i]);
				continue;
			}

			TArray<uint8> Indices;
			QuantizeFrame(Pixels, W, H, GifW, GifH, Indices);
			WriteFrame(Indices);
		}

		// Trailer
		Gif.Add(0x3B);

		if (!FFileHelper::SaveArrayToFile(Gif, *OutputPath))
		{
			UE_LOG(LogMCPPIE, Warning, TEXT("[GIF] Failed to write: %s"), *OutputPath);
			return false;
		}

		UE_LOG(LogMCPPIE, Log, TEXT("[GIF] Wrote %d frames -> %s (%.1f KB)"),
			FramePaths.Num(), *OutputPath, Gif.Num() / 1024.f);
		return true;
	}
}
