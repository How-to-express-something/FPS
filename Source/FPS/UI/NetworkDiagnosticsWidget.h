#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "NetworkDiagnosticsWidget.generated.h"

class UTextBlock;

/** Passive local-player panel. No timer, async task or gameplay subscription. */
UCLASS()
class FPS_API UNetworkDiagnosticsWidget : public UUserWidget
{
	GENERATED_BODY()
public:
	UFUNCTION(BlueprintCallable, Category="Diagnostics") void RefreshDiagnostics();
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeTick(const FGeometry& MyGeometry, float InDeltaTime) override;
	UPROPERTY(meta=(BindWidgetOptional)) TObjectPtr<UTextBlock> DiagnosticsText;
private:
	friend struct FFPSNetworkDiagnosticsTestAccess;
	float RefreshElapsed = 0.f;
};
