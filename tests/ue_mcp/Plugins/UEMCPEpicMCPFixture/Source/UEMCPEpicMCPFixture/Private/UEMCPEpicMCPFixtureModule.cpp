// Test-only tools and a resource provider registered with Epic's Unreal MCP
// plugin, so the live tests can prove ue-mcp lists, calls, cancels and reads
// them. Never shipped: it lives in the test project, not in the bridge.

#include "UEMCPFixtureToolLibrary.h"

#include "Containers/Ticker.h"
#include "IModelContextProtocolModule.h"
#include "IModelContextProtocolResourceProvider.h"
#include "IModelContextProtocolTool.h"
#include "ModelContextProtocolResources.h"
#include "ModelContextProtocolSession.h"
#include "ModelContextProtocolToolResults.h"
#include "Modules/ModuleManager.h"

namespace UEMCPFixture
{
	// A 1x1 PNG.
	static const TCHAR* const PixelPng = TEXT("iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mNk+M9QDwADhgGAWjR9awAAAABJRU5ErkJggg==");

	static TSharedPtr<FJsonObject> Schema(const TMap<FString, FString>& StringProps, const TMap<FString, FString>& NumberProps = {})
	{
		TSharedPtr<FJsonObject> Props = MakeShared<FJsonObject>();
		for (const TPair<FString, FString>& P : StringProps)
		{
			TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
			Prop->SetStringField(TEXT("type"), TEXT("string"));
			Prop->SetStringField(TEXT("description"), P.Value);
			Props->SetObjectField(P.Key, Prop);
		}
		for (const TPair<FString, FString>& P : NumberProps)
		{
			TSharedPtr<FJsonObject> Prop = MakeShared<FJsonObject>();
			Prop->SetStringField(TEXT("type"), TEXT("number"));
			Prop->SetStringField(TEXT("description"), P.Value);
			Props->SetObjectField(P.Key, Prop);
		}
		TSharedPtr<FJsonObject> Out = MakeShared<FJsonObject>();
		Out->SetStringField(TEXT("type"), TEXT("object"));
		Out->SetObjectField(TEXT("properties"), Props);
		return Out;
	}

	static FModelContextProtocolToolResult TextAndStructured(const FString& Text, const TSharedPtr<FJsonObject>& Structured)
	{
		TSharedPtr<FJsonObject> Block = MakeShared<FJsonObject>();
		Block->SetStringField(TEXT("type"), TEXT("text"));
		Block->SetStringField(TEXT("text"), Text);
		TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
		Result->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(Block) });
		if (Structured.IsValid()) Result->SetObjectField(TEXT("structuredContent"), Structured);
		return FModelContextProtocolToolResult(Result);
	}

	/** Echoes its message back as text and as structured content. */
	struct FEchoTool : IModelContextProtocolTool
	{
		virtual FString GetName() const override { return TEXT("ue_mcp_fixture_echo"); }
		virtual FString GetDescription() const override { return TEXT("Echoes message back. ue-mcp test fixture."); }
		virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override { return Schema({ { TEXT("message"), TEXT("Text to echo") } }); }
		virtual FModelContextProtocolToolResult Run(const TSharedPtr<FJsonObject>& Params) override
		{
			FString Message;
			if (Params.IsValid()) Params->TryGetStringField(TEXT("message"), Message);
			TSharedPtr<FJsonObject> Structured = MakeShared<FJsonObject>();
			Structured->SetStringField(TEXT("message"), Message);
			Structured->SetNumberField(TEXT("length"), Message.Len());
			return TextAndStructured(FString::Printf(TEXT("echo: %s"), *Message), Structured);
		}
	};

	/** Returns one image content block, the shape Epic's tools use for captures. */
	struct FImageTool : IModelContextProtocolTool
	{
		virtual FString GetName() const override { return TEXT("ue_mcp_fixture_image"); }
		virtual FString GetDescription() const override { return TEXT("Returns a 1x1 PNG as image content. ue-mcp test fixture."); }
		virtual FModelContextProtocolToolResult Run(const TSharedPtr<FJsonObject>& Params) override
		{
			TSharedPtr<FJsonObject> Caption = MakeShared<FJsonObject>();
			Caption->SetStringField(TEXT("type"), TEXT("text"));
			Caption->SetStringField(TEXT("text"), TEXT("one pixel"));
			TSharedPtr<FJsonObject> Image = MakeShared<FJsonObject>();
			Image->SetStringField(TEXT("type"), TEXT("image"));
			Image->SetStringField(TEXT("mimeType"), TEXT("image/png"));
			Image->SetStringField(TEXT("data"), PixelPng);
			TSharedPtr<FJsonObject> Result = MakeShared<FJsonObject>();
			Result->SetArrayField(TEXT("content"), { MakeShared<FJsonValueObject>(Caption), MakeShared<FJsonValueObject>(Image) });
			return FModelContextProtocolToolResult(Result);
		}
	};

	/** Answers after `seconds` unless cancelled first; counts the cancels it receives. */
	struct FSlowTool : IModelContextProtocolTool
	{
		struct FPending
		{
			FModelContextProtocolToolRequestId RequestId;
			FResultCallback OnComplete;
			double DueSeconds = 0.0;
			double Seconds = 0.0;
		};
		TArray<FPending> Pending;
		int32 Cancellations = 0;
		int32 Completions = 0;
		FTSTicker::FDelegateHandle Ticker;

		virtual FString GetName() const override { return TEXT("ue_mcp_fixture_slow"); }
		virtual FString GetDescription() const override { return TEXT("Answers after `seconds` (default 3) unless cancelled. ue-mcp test fixture."); }
		virtual TSharedPtr<FJsonObject> GetInputJsonSchema() const override { return Schema({}, { { TEXT("seconds"), TEXT("How long to take") } }); }

		virtual void RunAsync(const FModelContextProtocolToolRequestId& RequestId, const TSharedPtr<FJsonObject>& Params, const FResultCallback& OnComplete) override
		{
			double Seconds = 3.0;
			if (Params.IsValid()) Params->TryGetNumberField(TEXT("seconds"), Seconds);
			Pending.Add({ RequestId, OnComplete, FPlatformTime::Seconds() + Seconds, Seconds });
			if (!Ticker.IsValid())
			{
				Ticker = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FSlowTool::Tick), 0.05f);
			}
		}

		virtual void CancelAsync(const FModelContextProtocolToolRequestId& RequestId) override
		{
			// FModelContextProtocolToolRequestId's operator== is not exported, so compare the ids it wraps.
			const int32 Removed = Pending.RemoveAll([&RequestId](const FPending& P)
			{
				return P.RequestId.RequestId.IsValid() && RequestId.RequestId.IsValid()
					&& FJsonValue::CompareEqual(*P.RequestId.RequestId, *RequestId.RequestId);
			});
			Cancellations += Removed;
		}

		bool Tick(float)
		{
			const double Now = FPlatformTime::Seconds();
			for (int32 i = Pending.Num() - 1; i >= 0; --i)
			{
				if (Pending[i].DueSeconds > Now) continue;
				const FPending Done = Pending[i];
				Pending.RemoveAt(i);
				++Completions;
				TSharedPtr<FJsonObject> Structured = MakeShared<FJsonObject>();
				Structured->SetNumberField(TEXT("seconds"), Done.Seconds);
				Done.OnComplete(TextAndStructured(FString::Printf(TEXT("finished after %.1fs"), Done.Seconds), Structured));
			}
			if (Pending.Num() > 0) return true;
			Ticker.Reset();
			return false;
		}

		~FSlowTool()
		{
			if (Ticker.IsValid()) FTSTicker::GetCoreTicker().RemoveTicker(Ticker);
		}
	};

	/** Reports what the slow tool saw, so a test can prove a cancel reached it. */
	struct FSlowStateTool : IModelContextProtocolTool
	{
		TWeakPtr<FSlowTool> Slow;
		virtual FString GetName() const override { return TEXT("ue_mcp_fixture_slow_state"); }
		virtual FString GetDescription() const override { return TEXT("Counts the slow fixture's cancels, completions and pending calls. ue-mcp test fixture."); }
		virtual FModelContextProtocolToolResult Run(const TSharedPtr<FJsonObject>& Params) override
		{
			TSharedPtr<FJsonObject> Structured = MakeShared<FJsonObject>();
			if (TSharedPtr<FSlowTool> Pinned = Slow.Pin())
			{
				Structured->SetNumberField(TEXT("cancellations"), Pinned->Cancellations);
				Structured->SetNumberField(TEXT("completions"), Pinned->Completions);
				Structured->SetNumberField(TEXT("pending"), Pinned->Pending.Num());
			}
			return TextAndStructured(TEXT("slow fixture state"), Structured);
		}
	};

	/** Two resources: a text note and a binary blob. */
	struct FResourceProvider : IModelContextProtocolResourceProvider
	{
		static constexpr const TCHAR* NoteUri = TEXT("ue-mcp-fixture://notes/readme");
		static constexpr const TCHAR* BlobUri = TEXT("ue-mcp-fixture://data/blob");

		virtual void ListResources(FModelContextProtocolResourceDescriptorList& Out) const override
		{
			Out.Add(FModelContextProtocolResourceDescriptor(NoteUri, FString(TEXT("readme")), FString(TEXT("Fixture readme")), FString(TEXT("A text resource from the ue-mcp fixture provider")), FString(TEXT("text/plain"))), AsShared());
			Out.Add(FModelContextProtocolResourceDescriptor(BlobUri, FString(TEXT("blob")), FString(TEXT("Fixture blob")), FString(TEXT("Five bytes from the ue-mcp fixture provider")), FString(TEXT("application/octet-stream"))), AsShared());
		}

		virtual TValueOrError<FModelContextProtocolResource, FString> ReadResource(const FString& Uri) const override
		{
			if (Uri == NoteUri)
			{
				return MakeValue(FModelContextProtocolResource(Uri, FString(TEXT("UE-MCP fixture resource text")), FString(TEXT("readme")), FString(TEXT("Fixture readme")), FString(TEXT("text/plain"))));
			}
			if (Uri == BlobUri)
			{
				uint8 Bytes[] = { 0, 1, 2, 3, 250 };
				return MakeValue(FModelContextProtocolResource(Uri, TArrayView<uint8>(Bytes), FString(TEXT("blob")), FString(TEXT("Fixture blob")), FString(TEXT("application/octet-stream"))));
			}
			return MakeError(FString::Printf(TEXT("unknown uri %s"), *Uri));
		}
	};
}

FString UUEMCPFixtureToolLibrary::UeMcpFixtureLibraryGreet(const FString& Name)
{
	return FString::Printf(TEXT("hello, %s"), *Name);
}

class FUEMCPEpicMCPFixtureModule : public IModuleInterface
{
public:
	virtual void StartupModule() override
	{
		TSharedRef<UEMCPFixture::FSlowTool> Slow = MakeShared<UEMCPFixture::FSlowTool>();
		TSharedRef<UEMCPFixture::FSlowStateTool> SlowState = MakeShared<UEMCPFixture::FSlowStateTool>();
		SlowState->Slow = Slow;
		Tools = { MakeShared<UEMCPFixture::FEchoTool>(), MakeShared<UEMCPFixture::FImageTool>(), Slow, SlowState };
		Provider = MakeShared<UEMCPFixture::FResourceProvider>();

		IModelContextProtocolModule& Module = IModelContextProtocolModule::GetChecked();
		AddTools(Module);
		Module.AddResourceProvider(Provider.ToSharedRef());
		// RefreshTools drops every tool; providers put theirs back.
		RefreshHandle = Module.OnRefreshTools().AddRaw(this, &FUEMCPEpicMCPFixtureModule::OnRefresh);
	}

	virtual void ShutdownModule() override
	{
		if (IModelContextProtocolModule* Module = IModelContextProtocolModule::Get())
		{
			Module->OnRefreshTools().Remove(RefreshHandle);
			for (const TSharedRef<IModelContextProtocolTool>& Tool : Tools) Module->RemoveTool(Tool);
			if (Provider.IsValid()) Module->RemoveResourceProvider(Provider.ToSharedRef());
		}
		Tools.Reset();
		Provider.Reset();
	}

private:
	void AddTools(IModelContextProtocolModule& Module)
	{
		for (const TSharedRef<IModelContextProtocolTool>& Tool : Tools) Module.AddTool(Tool);
	}

	void OnRefresh()
	{
		if (IModelContextProtocolModule* Module = IModelContextProtocolModule::Get()) AddTools(*Module);
	}

	TArray<TSharedRef<IModelContextProtocolTool>> Tools;
	TSharedPtr<IModelContextProtocolResourceProvider> Provider;
	FDelegateHandle RefreshHandle;
};

IMPLEMENT_MODULE(FUEMCPEpicMCPFixtureModule, UEMCPEpicMCPFixture)
