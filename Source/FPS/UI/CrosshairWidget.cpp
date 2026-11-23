#include "CrosshairWidget.h"
#include "Rendering/DrawElements.h"

void UCrosshairWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetVisibility(ESlateVisibility::HitTestInvisible);
}

int32 UCrosshairWidget::NativePaint(const FPaintArgs& Args, const FGeometry& AllottedGeometry,
	const FSlateRect& MyCullingRect, FSlateWindowElementList& OutDrawElements, int32 LayerId,
	const FWidgetStyle& InWidgetStyle, bool bParentEnabled) const
{
	const int32 BaseLayer = Super::NativePaint(Args, AllottedGeometry, MyCullingRect, OutDrawElements,
		LayerId, InWidgetStyle, bParentEnabled);
	const FVector2D Center = AllottedGeometry.GetLocalSize() * 0.5;
	const float SafeGap = FMath::Clamp(Gap, 0.f, 16.f);
	const float SafeLength = FMath::Clamp(LineLength, 1.f, 32.f);
	const float SafeThickness = FMath::Clamp(Thickness, 0.5f, 4.f);
	const FLinearColor Tint = InWidgetStyle.GetColorAndOpacityTint();
	for (const FVector2D Direction : {FVector2D(1, 0), FVector2D(-1, 0), FVector2D(0, 1), FVector2D(0, -1)})
	{
		const TArray<FVector2D> Points = {Center + Direction * SafeGap, Center + Direction * (SafeGap + SafeLength)};
		FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 1, AllottedGeometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, FLinearColor(0.f, 0.f, 0.f, 0.85f) * Tint, true, SafeThickness + 2.f);
		FSlateDrawElement::MakeLines(OutDrawElements, BaseLayer + 2, AllottedGeometry.ToPaintGeometry(), Points,
			ESlateDrawEffect::None, Color * Tint, true, SafeThickness);
	}
	return BaseLayer + 2;
}
