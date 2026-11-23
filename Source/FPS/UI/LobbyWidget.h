#pragma once
#include "CoreMinimal.h"
#include "Blueprint/UserWidget.h"
#include "LobbyWidget.generated.h"

class UButton;
class UTextBlock;

UCLASS()
class FPS_API ULobbyWidget : public UUserWidget
{
	GENERATED_BODY()
protected:
	virtual void NativeOnInitialized() override;
	virtual void NativeConstruct() override;
	virtual void NativeDestruct() override;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> ReadyButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> UnreadyButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> StartButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UButton> LeaveButton;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UTextBlock> PlayersText;
	UPROPERTY(meta=(BindWidgetOptional), BlueprintReadOnly) TObjectPtr<UTextBlock> StatusText;
private:
	FTimerHandle RefreshTimer;
	UFUNCTION() void ReadyClicked();
	UFUNCTION() void UnreadyClicked();
	UFUNCTION() void StartClicked();
	UFUNCTION() void LeaveClicked();
	void Refresh();
};
