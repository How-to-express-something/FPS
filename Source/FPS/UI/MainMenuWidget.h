#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "../Online/FPSSessionTypes.h"
#include "MainMenuWidget.generated.h"

class UButton;
class UTextBlock;
class UEditableTextBox;
class UComboBoxString;
class USpinBox;
class UMultiplayerSessionsSubsystem;

UCLASS()
class FPS_API UMainMenuWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> CreateButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> SearchButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> JoinButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> CancelButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> CleanupButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UEditableTextBox> RoomNameInput;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<USpinBox> CapacityInput;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UComboBoxString> RoomSelection;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UTextBlock> StatusText;
private:
	TWeakObjectPtr<UMultiplayerSessionsSubsystem> Sessions;
	UPROPERTY(Transient) TArray<FFPSRoomSearchResult> DisplayedRooms;
	UFUNCTION() void CreateClicked();
	UFUNCTION() void SearchClicked();
	UFUNCTION() void JoinClicked();
	UFUNCTION() void CancelClicked();
	UFUNCTION() void CleanupClicked();
	UFUNCTION() void RefreshState();
	UFUNCTION() void RefreshRooms();
};
