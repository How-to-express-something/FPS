#include "MainMenuWidget.h"
#include "SessionWidgetHelpers.h"
#include "../Online/MultiplayerSessionsSubsystem.h"
#include "Components/EditableTextBox.h"
#include "Components/ComboBoxString.h"
#include "Components/SpinBox.h"
#include "Engine/GameInstance.h"
#include "GameFramework/PlayerController.h"

void UMainMenuWidget::NativeOnInitialized()
{
	Super::NativeOnInitialized();
	SetIsFocusable(true);
	if (WidgetTree && !WidgetTree->RootWidget)
	{
		UVerticalBox* Box = FPSSessionUI::BuildPanel(WidgetTree);
		FPSSessionUI::AddText(WidgetTree, Box, TEXT("FPS - LAN Sessions"));
		RoomNameInput = WidgetTree->ConstructWidget<UEditableTextBox>(UEditableTextBox::StaticClass(), TEXT("RoomNameInput"));
		RoomNameInput->SetText(FText::FromString(TEXT("FPS Room")));
		Box->AddChildToVerticalBox(RoomNameInput);
		FPSSessionUI::AddText(WidgetTree, Box, TEXT("Total players (includes host):"));
		CapacityInput = WidgetTree->ConstructWidget<USpinBox>(USpinBox::StaticClass(), TEXT("CapacityInput"));
		CapacityInput->SetMinValue(2.f); CapacityInput->SetMaxValue(16.f); CapacityInput->SetDelta(1.f); CapacityInput->SetValue(4.f);
		Box->AddChildToVerticalBox(CapacityInput);
		CreateButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("CreateButton"), TEXT("Create room"));
		SearchButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("SearchButton"), TEXT("Search LAN"));
		RoomSelection = WidgetTree->ConstructWidget<UComboBoxString>(UComboBoxString::StaticClass(), TEXT("RoomSelection"));
		Box->AddChildToVerticalBox(RoomSelection);
		JoinButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("JoinButton"), TEXT("Join selected room"));
		CancelButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("CancelButton"), TEXT("Cancel pending operation"));
		CleanupButton = FPSSessionUI::AddButton(WidgetTree, Box, TEXT("CleanupButton"), TEXT("Clean up local session"));
		StatusText = FPSSessionUI::AddText(WidgetTree, Box, TEXT("Ready"));
		StatusText->SetAutoWrapText(true);
	}
	if (CreateButton) CreateButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CreateClicked);
	if (SearchButton) SearchButton->OnClicked.AddUniqueDynamic(this, &ThisClass::SearchClicked);
	if (JoinButton) JoinButton->OnClicked.AddUniqueDynamic(this, &ThisClass::JoinClicked);
	if (CancelButton) CancelButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CancelClicked);
	if (CleanupButton) CleanupButton->OnClicked.AddUniqueDynamic(this, &ThisClass::CleanupClicked);
}

void UMainMenuWidget::NativeConstruct()
{
	Super::NativeConstruct();
	Sessions = GetGameInstance() ? GetGameInstance()->GetSubsystem<UMultiplayerSessionsSubsystem>() : nullptr;
	if (Sessions.IsValid())
	{
		Sessions->OnStateChanged.AddUniqueDynamic(this, &ThisClass::RefreshState);
		Sessions->OnRoomsChanged.AddUniqueDynamic(this, &ThisClass::RefreshRooms);
	}
	RefreshRooms();
	RefreshState();
}

void UMainMenuWidget::NativeDestruct()
{
	if (Sessions.IsValid())
	{
		Sessions->OnStateChanged.RemoveDynamic(this, &ThisClass::RefreshState);
		Sessions->OnRoomsChanged.RemoveDynamic(this, &ThisClass::RefreshRooms);
	}
	Sessions.Reset();
	Super::NativeDestruct();
}

void UMainMenuWidget::CreateClicked()
{
	if (Sessions.IsValid()) Sessions->CreateRoom(RoomNameInput ? RoomNameInput->GetText().ToString() : TEXT("FPS Room"), CapacityInput ? FMath::RoundToInt(CapacityInput->GetValue()) : 4);
}
void UMainMenuWidget::SearchClicked() { if (Sessions.IsValid()) Sessions->FindRooms(); }
void UMainMenuWidget::CancelClicked() { if (Sessions.IsValid()) Sessions->CancelPendingOperation(); }
void UMainMenuWidget::CleanupClicked() { if (Sessions.IsValid()) Sessions->LeaveRoom(); }
void UMainMenuWidget::JoinClicked()
{
	const int32 Index = RoomSelection ? RoomSelection->GetSelectedIndex() : INDEX_NONE;
	if (Sessions.IsValid() && DisplayedRooms.IsValidIndex(Index))
		Sessions->JoinRoom(DisplayedRooms[Index].Generation, DisplayedRooms[Index].ResultIndex);
}

void UMainMenuWidget::RefreshState()
{
	const bool bIdle = Sessions.IsValid() && !Sessions->IsBusy() && UMultiplayerSessionsSubsystem::IsMenuOperationAllowed(Sessions->GetFlow());
	if (CreateButton) CreateButton->SetIsEnabled(bIdle);
	if (SearchButton) SearchButton->SetIsEnabled(bIdle);
	if (JoinButton) JoinButton->SetIsEnabled(bIdle && !DisplayedRooms.IsEmpty());
	if (RoomSelection) RoomSelection->SetIsEnabled(bIdle);
	if (CancelButton) CancelButton->SetIsEnabled(Sessions.IsValid() && Sessions->IsBusy() && Sessions->GetFlow() != EFPSSessionFlow::Recovering && Sessions->GetFlow() != EFPSSessionFlow::Destroying);
	if (CleanupButton) CleanupButton->SetIsEnabled(bIdle);
	if (StatusText)
	{
		const FString Text = Sessions.IsValid()
			? FString::Printf(TEXT("%s | %d room(s)\n%s"), *UEnum::GetValueAsString(Sessions->GetFlow()), DisplayedRooms.Num(), *Sessions->GetLastError())
			: TEXT("Session subsystem unavailable");
		StatusText->SetText(FText::FromString(Text));
	}
}

void UMainMenuWidget::RefreshRooms()
{
	DisplayedRooms = Sessions.IsValid() ? Sessions->GetRooms() : TArray<FFPSRoomSearchResult>();
	if (RoomSelection)
	{
		RoomSelection->ClearOptions();
		for (const FFPSRoomSearchResult& R : DisplayedRooms)
			RoomSelection->AddOption(FString::Printf(TEXT("[%d] %s  %d/%d  %dms%s"), R.ResultIndex, *R.RoomName, R.PlayerCount, R.Capacity, R.PingMs, R.bCompatible ? TEXT("") : TEXT(" [incompatible]")));
		if (!DisplayedRooms.IsEmpty()) RoomSelection->SetSelectedIndex(0);
	}
	RefreshState();
}
