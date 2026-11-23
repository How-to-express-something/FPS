#pragma once

#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "CrosshairWidget.generated.h"

/** Fixed screen-space reticle. No combat, spread, recoil or weapon subscriptions. */
UCLASS()
class FPS_API UCrosshairWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
	virtual int32 NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
		const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const override;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0", ClampMax = "16"))
	float Gap = 3.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "1", ClampMax = "32"))
	float LineLength = 7.f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair", meta = (ClampMin = "0.5", ClampMax = "4"))
	float Thickness = 1.5f;

	UPROPERTY(EditDefaultsOnly, BlueprintReadOnly, Category = "Crosshair")
	FLinearColor Color = FLinearColor::White;
};
