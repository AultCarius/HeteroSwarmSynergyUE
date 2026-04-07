// Copyright Epic Games, Inc. All Rights Reserved.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/Actor.h"
#include "EventMarkerManager.h"
#include "EventMarkerBase.generated.h"

UCLASS(Blueprintable)
class HETEROSWARMSYNERGYUE_API AEventMarkerBase : public AActor
{
    GENERATED_BODY()

public:
    AEventMarkerBase();

protected:
    virtual void BeginPlay() override;

public:
    virtual void Tick(float DeltaSeconds) override;

public:
    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Event Marker")
    void InitializeEventMarker(int32 InEventID, uint8 InEventType);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Event Marker")
    virtual void ApplyEventState(const FEventMarkerRuntimeState& InState);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Event Marker")
    virtual void SetHighlighted(bool bInHighlighted);

    UFUNCTION(BlueprintCallable, Category = "Hetero Swarm|Event Marker")
    virtual void PlayDisappear();

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Event Marker")
    int32 GetEventID() const { return EventID; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Event Marker")
    uint8 GetEventType() const { return EventType; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Event Marker")
    bool IsHighlighted() const { return bHighlighted; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Event Marker")
    bool HasValidRuntimeState() const { return bHasValidRuntimeState; }

    UFUNCTION(BlueprintPure, Category = "Hetero Swarm|Event Marker")
    FEventMarkerRuntimeState GetCurrentRuntimeState() const { return CurrentRuntimeState; }

protected:
    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Components")
    USceneComponent* RootSceneComponent;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker")
    int32 EventID;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker")
    uint8 EventType;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker")
    bool bHighlighted;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker")
    bool bHasValidRuntimeState;

    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker")
    bool bDestroyImmediatelyOnDisappear;

    /** 是否自动对所有 PrimitiveComponent 切换 CustomDepth */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker|Highlight")
    bool bAutoToggleCustomDepth;

    /** 高亮时写入的 Stencil 值（需配合项目中的后处理材质使用） */
    UPROPERTY(EditAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker|Highlight",
        meta = (EditCondition = "bAutoToggleCustomDepth", ClampMin = "0", ClampMax = "255"))
    int32 HighlightStencilValue;

    UPROPERTY(VisibleAnywhere, BlueprintReadOnly, Category = "Hetero Swarm|Event Marker")
    FEventMarkerRuntimeState CurrentRuntimeState;

protected:
    /** 对 Actor 下的 PrimitiveComponent 统一切换 CustomDepth / Stencil */
    void ApplyHighlightRenderState(bool bEnable);

protected:
    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Event Marker")
    void BP_OnEventInitialized();

    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Event Marker")
    void BP_OnEventStateApplied(const FEventMarkerRuntimeState& NewState);

    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Event Marker")
    void BP_OnHighlightChanged(bool bNewHighlighted);

    UFUNCTION(BlueprintImplementableEvent, Category = "Hetero Swarm|Event Marker")
    void BP_OnDisappear();
};
