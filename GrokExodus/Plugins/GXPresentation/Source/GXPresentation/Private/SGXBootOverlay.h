// Copyright Grok Exodus. All Rights Reserved.
#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

/** Full-screen boot overlay + always-on version stamp. */
class SGXBootOverlay : public SCompoundWidget
{
public:
	SLATE_BEGIN_ARGS(SGXBootOverlay) {}
	SLATE_END_ARGS()

	void Construct(const FArguments& InArgs);
	void SetState(float InOverlayAlpha, float InProgress, const FString& InStatus, const FString& InExtra);
	void SetFlight(const FString& InFlight);

	struct FStarDot
	{
		FVector2f UV = FVector2f::ZeroVector;
		float SizePx = 2.0f;
		float Alpha = 1.0f;
	};
	void SetStars(const TArray<FStarDot>& InStars);

	virtual int32 OnPaint(
		const FPaintArgs& Args,
		const FGeometry& AllottedGeometry,
		const FSlateRect& MyCullingRect,
		FSlateWindowElementList& OutDrawElements,
		int32 LayerId,
		const FWidgetStyle& InWidgetStyle,
		bool bParentEnabled) const override;

private:
	FSlateColor GetDimColor() const;
	EVisibility GetLoadVisibility() const;
	TOptional<float> GetProgress() const;

	float OverlayAlpha = 1.0f;
	float Progress = 0.05f;

	TSharedPtr<STextBlock> BuildLine;
	TSharedPtr<STextBlock> StatusLine;
	TSharedPtr<STextBlock> Stamp;
	TSharedPtr<STextBlock> Flight;
	TArray<FStarDot> Stars;
};
