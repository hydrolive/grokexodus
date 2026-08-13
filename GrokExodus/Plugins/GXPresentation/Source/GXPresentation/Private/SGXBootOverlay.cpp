// Copyright Grok Exodus. All Rights Reserved.

#include "SGXBootOverlay.h"
#include "GXVersion.h"
#include "Styling/CoreStyle.h"
#include "Styling/SlateBrush.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/SOverlay.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SBox.h"
#include "Widgets/Notifications/SProgressBar.h"
#include "Widgets/Text/STextBlock.h"

namespace
{
	static FSlateColorBrush GXBootDimBrush(FLinearColor::White);
}

void SGXBootOverlay::Construct(const FArguments& InArgs)
{
	SetVisibility(EVisibility::HitTestInvisible);
	SetCanTick(false);

	ChildSlot
	[
		SNew(SOverlay)
		+ SOverlay::Slot()
		.HAlign(HAlign_Fill)
		.VAlign(VAlign_Fill)
		[
			SNew(SBorder)
			.BorderImage(&GXBootDimBrush)
			.BorderBackgroundColor(this, &SGXBootOverlay::GetDimColor)
			.Visibility(this, &SGXBootOverlay::GetLoadVisibility)
			.Padding(FMargin(80.f, 0.f))
			[
				SNew(SBox)
				.HAlign(HAlign_Left)
				.VAlign(VAlign_Center)
				[
					SNew(SVerticalBox)
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(STextBlock)
						.Text(FText::FromString(TEXT("GROK EXODUS")))
						.Font(FCoreStyle::GetDefaultFontStyle("Bold", 36))
						.ColorAndOpacity(FLinearColor(0.94f, 0.96f, 0.98f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 12.f, 0.f, 0.f)
					[
						SAssignNew(BuildLine, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 16))
						.ColorAndOpacity(FLinearColor(0.63f, 0.75f, 0.82f))
					]
					+ SVerticalBox::Slot().AutoHeight().Padding(0.f, 28.f, 0.f, 8.f)
					[
						SAssignNew(StatusLine, STextBlock)
						.Font(FCoreStyle::GetDefaultFontStyle("Regular", 18))
						.ColorAndOpacity(FLinearColor(0.86f, 0.90f, 0.92f))
					]
					+ SVerticalBox::Slot().AutoHeight()
					[
						SNew(SHorizontalBox)
						+ SHorizontalBox::Slot().AutoWidth()
						[
							SNew(SBox)
							.WidthOverride(640.f)
							.HeightOverride(22.f)
							[
								SNew(SProgressBar)
								.Percent(this, &SGXBootOverlay::GetProgress)
								.FillColorAndOpacity(FLinearColor(0.30f, 0.78f, 0.48f))
							]
						]
					]
				]
			]
		]
		+ SOverlay::Slot()
		.HAlign(HAlign_Left)
		.VAlign(VAlign_Top)
		.Padding(FMargin(16.f, 14.f))
		[
			SAssignNew(Stamp, STextBlock)
			.Font(FCoreStyle::GetDefaultFontStyle("Bold", 16))
			.ColorAndOpacity(FLinearColor(1.f, 0.86f, 0.31f))
			.ShadowOffset(FVector2D(1.f, 1.f))
			.ShadowColorAndOpacity(FLinearColor(0.f, 0.f, 0.f, 0.85f))
		]
	];

	SetState(1.0f, 0.05f, TEXT("Starting planet systems…"), TEXT("boot"));
}

void SGXBootOverlay::SetState(float InOverlayAlpha, float InProgress, const FString& InStatus, const FString& InExtra)
{
	OverlayAlpha = FMath::Clamp(InOverlayAlpha, 0.0f, 1.0f);
	Progress = FMath::Clamp(InProgress, 0.0f, 1.0f);

	if (BuildLine.IsValid())
	{
		BuildLine->SetText(FText::FromString(
			FString::Printf(TEXT("Build %s   %s"), GX_VERSION_STRING, GX_VERSION_DATE)));
	}
	if (StatusLine.IsValid())
	{
		StatusLine->SetText(FText::FromString(
			FString::Printf(TEXT("%s   %d%%"), *InStatus, FMath::RoundToInt(Progress * 100.f))));
	}
	if (Stamp.IsValid())
	{
		Stamp->SetText(FText::FromString(
			FString::Printf(TEXT("GX %s  %s"), GX_VERSION_STRING, *InExtra)));
	}
}

FSlateColor SGXBootOverlay::GetDimColor() const
{
	return FLinearColor(0.01f, 0.015f, 0.03f, OverlayAlpha);
}

EVisibility SGXBootOverlay::GetLoadVisibility() const
{
	return OverlayAlpha > 0.01f ? EVisibility::HitTestInvisible : EVisibility::Collapsed;
}

TOptional<float> SGXBootOverlay::GetProgress() const
{
	return Progress;
}
