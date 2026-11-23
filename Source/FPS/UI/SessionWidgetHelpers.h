#pragma once
#include "Blueprint/WidgetTree.h"
#include "Components/Border.h"
#include "Components/VerticalBox.h"
#include "Components/VerticalBoxSlot.h"
#include "Components/Button.h"
#include "Components/TextBlock.h"

namespace FPSSessionUI
{
	inline UVerticalBox* BuildPanel(UWidgetTree* Tree)
	{
		UBorder* Root = Tree->ConstructWidget<UBorder>();
		Root->SetBrushColor(FLinearColor(.015f, .02f, .03f, 1.f));
		Root->SetPadding(FMargin(60.f));
		Root->SetHorizontalAlignment(HAlign_Center);
		Root->SetVerticalAlignment(VAlign_Center);
		Tree->RootWidget = Root;
		UVerticalBox* Box = Tree->ConstructWidget<UVerticalBox>();
		Root->SetContent(Box);
		return Box;
	}
	inline UTextBlock* AddText(UWidgetTree* Tree, UVerticalBox* Box, const FString& Text)
	{
		UTextBlock* W = Tree->ConstructWidget<UTextBlock>();
		W->SetText(FText::FromString(Text));
		Box->AddChildToVerticalBox(W)->SetPadding(FMargin(6.f));
		return W;
	}
	inline UButton* AddButton(UWidgetTree* Tree, UVerticalBox* Box, const FName Name, const FString& Label)
	{
		UButton* Button = Tree->ConstructWidget<UButton>(UButton::StaticClass(), Name);
		UTextBlock* Text = Tree->ConstructWidget<UTextBlock>();
		Text->SetText(FText::FromString(Label));
		Button->SetContent(Text);
		Box->AddChildToVerticalBox(Button)->SetPadding(FMargin(6.f));
		return Button;
	}
}
