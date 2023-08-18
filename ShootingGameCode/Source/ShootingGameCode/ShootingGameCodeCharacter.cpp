// Copyright Epic Games, Inc. All Rights Reserved.

#include "ShootingGameCodeCharacter.h"
#include "Camera/CameraComponent.h"
#include "Components/CapsuleComponent.h"
#include "Components/InputComponent.h"
#include "GameFramework/CharacterMovementComponent.h"
#include "GameFramework/Controller.h"
#include "GameFramework/SpringArmComponent.h"
#include "EnhancedInputComponent.h"
#include "EnhancedInputSubsystems.h"
#include "Kismet/GameplayStatics.h"
#include "Net/UnrealNetwork.h"
#include "Weapon.h"
#include "ShootingPlayerState.h"
#include "GameFramework/PlayerStart.h"
#include "Blueprint/UserWidget.h"
#include "CustomUserWidget.h"
#include "Grenade.h"

//////////////////////////////////////////////////////////////////////////
// AShootingGameCodeCharacter

AShootingGameCodeCharacter::AShootingGameCodeCharacter()
{
	// Set size for collision capsule
	GetCapsuleComponent()->InitCapsuleSize(42.f, 96.0f);
		
	// Don't rotate when the controller rotates. Let that just affect the camera.
	bUseControllerRotationPitch = false;
	bUseControllerRotationYaw = false;
	bUseControllerRotationRoll = false;

	// Configure character movement
	GetCharacterMovement()->bOrientRotationToMovement = true; // Character moves in the direction of input...	
	GetCharacterMovement()->RotationRate = FRotator(0.0f, 500.0f, 0.0f); // ...at this rotation rate

	// Note: For faster iteration times these variables, and many more, can be tweaked in the Character Blueprint
	// instead of recompiling to adjust them
	GetCharacterMovement()->JumpZVelocity = 700.f;
	GetCharacterMovement()->AirControl = 0.35f;
	GetCharacterMovement()->MaxWalkSpeed = 500.f;
	GetCharacterMovement()->MinAnalogWalkSpeed = 20.f;
	GetCharacterMovement()->BrakingDecelerationWalking = 2000.f;

	// Create a camera boom (pulls in towards the player if there is a collision)
	CameraBoom = CreateDefaultSubobject<USpringArmComponent>(TEXT("CameraBoom"));
	CameraBoom->SetupAttachment(RootComponent);
	CameraBoom->TargetArmLength = 400.0f; // The camera follows at this distance behind the character	
	CameraBoom->bUsePawnControlRotation = true; // Rotate the arm based on the controller

	// Create a follow camera
	FollowCamera = CreateDefaultSubobject<UCameraComponent>(TEXT("FollowCamera"));
	FollowCamera->SetupAttachment(CameraBoom, USpringArmComponent::SocketName); // Attach the camera to the end of the boom and let the boom adjust to match the controller orientation
	FollowCamera->bUsePawnControlRotation = false; // Camera does not rotate relative to arm

	// Note: The skeletal mesh and anim blueprint references on the Mesh component (inherited from Character) 
	// are set in the derived blueprint asset named ThirdPersonCharacter (to avoid direct content references in C++)

	GetMesh()->SetCollisionProfileName("Ragdoll");
	IsRagdoll = false;

	OnDestroyed.AddDynamic(this, &AShootingGameCodeCharacter::OnCharacterDestroyed);
}

void AShootingGameCodeCharacter::GetLifetimeReplicatedProps(TArray<FLifetimeProperty>& OutLifetimeProps) const
{
	Super::GetLifetimeReplicatedProps(OutLifetimeProps);

	DOREPLIFETIME(AShootingGameCodeCharacter, PlayerRotation);
}

void AShootingGameCodeCharacter::BeginPlay()
{
	// Call the base class  
	Super::BeginPlay();

	//Add Input Mapping Context
	if (APlayerController* PlayerController = Cast<APlayerController>(Controller))
	{
		if (UEnhancedInputLocalPlayerSubsystem* Subsystem = ULocalPlayer::GetSubsystem<UEnhancedInputLocalPlayerSubsystem>(PlayerController->GetLocalPlayer()))
		{
			Subsystem->AddMappingContext(DefaultMappingContext, 0);
		}
	}

	CreateNameTag();
	BindPlayerState();
}

void AShootingGameCodeCharacter::Tick(float DeltaTime)
{
	Super::Tick(DeltaTime);

	if (HasAuthority() == true)
	{
		PlayerRotation = GetControlRotation();
	}

	if (UGameplayStatics::GetPlayerCharacter(GetWorld(), 0) == this && IsRagdoll)
	{
		SetActorLocation(GetMesh()->GetSocketLocation("spine_02"), true);
	}
}

float AShootingGameCodeCharacter::TakeDamage(float DamageAmount, FDamageEvent const& DamageEvent, AController* EventInstigator, AActor* DamageCauser)
{
	if (EventInstigator == nullptr)
		return 0.0f;

	GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow,
		FString::Printf(TEXT("TakeDamage EventInstigator=%s"), *EventInstigator->GetName()));

	AShootingPlayerState* ps = Cast<AShootingPlayerState>(GetPlayerState());
	if (IsValid(ps) == false)
		return 0.0f;

	ps->AddDamage(DamageAmount);

	return DamageAmount;
}

void AShootingGameCodeCharacter::OnCharacterDestroyed(AActor* DestroyedActor)
{
	if (IsValid(NameTagWidget) == false)
		return;

	NameTagWidget->RemoveFromParent();
}

void AShootingGameCodeCharacter::ReqReload_Implementation()
{
	AShootingPlayerState* ps = Cast<AShootingPlayerState>(GetPlayerState());
	if (IsValid(ps) == false)
		return;

	if (ps->IsCanUseMag() == false)
		return;

	ResReload();
}

void AShootingGameCodeCharacter::ResReload_Implementation()
{
	IWeaponInterface* InterfaceObj = Cast<IWeaponInterface>(EquipWeapon);

	if (InterfaceObj == nullptr)
		return;

	InterfaceObj->Execute_EventReload(EquipWeapon);
}

void AShootingGameCodeCharacter::ReqTrigger_Implementation(bool IsPress)
{
	ResTrigger(IsPress);
}

void AShootingGameCodeCharacter::ResTrigger_Implementation(bool IsPress)
{
	IWeaponInterface* InterfaceObj = Cast<IWeaponInterface>(EquipWeapon);

	if (InterfaceObj == nullptr)
		return;

	InterfaceObj->Execute_EventTrigger(EquipWeapon, IsPress);
}

void AShootingGameCodeCharacter::ReqPressF_Implementation()
{
	//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Blue, TEXT("ReqPressF_Implementation"));
	AActor* nearestWeapon = FindNearestWeapon();

	if (IsValid(nearestWeapon) == false)
		return;

	nearestWeapon->SetOwner(GetController());

	ResPressF(nearestWeapon);
}

void AShootingGameCodeCharacter::ResPressF_Implementation(AActor* weapon)
{
	if (IsValid(EquipWeapon))
	{
		DoDrop();
	}

	DoPickUp(weapon);
}

void AShootingGameCodeCharacter::ReqDrop_Implementation()
{
	ResDrop();
}

void AShootingGameCodeCharacter::ResDrop_Implementation()
{
	DoDrop();
}

void AShootingGameCodeCharacter::ResRevive_Implementation(FTransform ReviveTrans)
{
	//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Yellow,
	//	FString::Printf(TEXT("ResRevive")));

	DoGetUp();

	FVector Loc = ReviveTrans.GetLocation();
	FRotator Rot = ReviveTrans.Rotator();

	SetActorLocationAndRotation(Loc, Rot);
}

void AShootingGameCodeCharacter::ReqGrenade_Implementation()
{
	ResGrenade();
}

void AShootingGameCodeCharacter::ResGrenade_Implementation()
{
	PlayAnimMontage(GrenadeMontage);
}

void AShootingGameCodeCharacter::ReqSpawnGrenade_Implementation(FVector Start, FVector Impluse)
{
	AGrenade* grenade = GetWorld()->SpawnActor<AGrenade>(GrenadeClass, Start, FRotator(0.0f, 0.0f, 0.0f));

	grenade->instigater = GetController();
	grenade->StaticMesh->AddImpulse(Impluse);
}

void AShootingGameCodeCharacter::EventGetItem_Implementation(EItemType itemType)
{
	switch (itemType)
	{
		case EItemType::IT_Heal:
		{
			AShootingPlayerState* ps = Cast<AShootingPlayerState>(GetPlayerState());
			if (IsValid(ps))
			{
				ps->AddHeal(100);
			}
			break;
		}
		case EItemType::IT_Mag:
		{
			AShootingPlayerState* ps = Cast<AShootingPlayerState>(GetPlayerState());
			if (IsValid(ps))
			{
				ps->AddMag();
			}
			break;
		}
	}
}

void AShootingGameCodeCharacter::EventUpdateNameTag_Implementation()
{
}

void AShootingGameCodeCharacter::EquipTestWeapon(TSubclassOf<class AWeapon> WeaponClass)
{
	EquipWeapon = GetWorld()->SpawnActor<AWeapon>(WeaponClass, FVector(0, 0, 0), FRotator(0, 0, 0));

	AWeapon* pWeapon = Cast<AWeapon>(EquipWeapon);
	if (IsValid(pWeapon) == false)
		return;

	pWeapon->OwnChar = this;

	EquipWeapon->AttachToComponent(GetMesh(),
		FAttachmentTransformRules::SnapToTargetNotIncludingScale, FName("weapon"));
}

AActor* AShootingGameCodeCharacter::FindNearestWeapon()
{
	TArray<AActor*> actors;
	GetCapsuleComponent()->GetOverlappingActors(actors, AWeapon::StaticClass());

	double nearestLength = 99999999.0f;
	AActor* nearestWeapon = nullptr;

	for (AActor* target : actors)
	{
		bool IsCanPickUp = false;
		IWeaponInterface* i = Cast<IWeaponInterface>(target);
		i->Execute_IsCanPickUp(target, IsCanPickUp);
		if (IsCanPickUp == false)
			continue;

		double distance = FVector::Dist(target->GetActorLocation(), GetActorLocation());

		if (nearestLength < distance)
			continue;

		nearestLength = distance;
		nearestWeapon = target;
	}

	return nearestWeapon;
}

void AShootingGameCodeCharacter::DoRagdoll()
{
	if (IsRagdoll)
		return;

	IsRagdoll = true;

	GetMesh()->SetSimulatePhysics(true);
}

void AShootingGameCodeCharacter::DoGetUp()
{
	if (IsRagdoll == false)
		return;

	IsRagdoll = false;

	GetMesh()->SetSimulatePhysics(false);

	GetMesh()->AttachToComponent(GetRootComponent(), FAttachmentTransformRules::KeepRelativeTransform);

	FVector vloc = { 0.0f, 0.0f, -89.0f };
	FRotator rRot = { 0.0f, 270.0f, 0.0f };

	GetMesh()->SetRelativeLocationAndRotation(vloc, rRot);
}

void AShootingGameCodeCharacter::OnUpdateHp_Implementation(float CurHp, float MaxHp)
{
	if (CurHp > 0)
	{

	}
	else
	{
		DoRagdoll();

		if (HasAuthority())
		{
			FTimerManager& timerManager = GetWorld()->GetTimerManager();
			timerManager.SetTimer(th_Revive, this, &AShootingGameCodeCharacter::DoRevive, 3.0f, false);
		}
	}
}

void AShootingGameCodeCharacter::DoPickUp(AActor* weapon)
{
	//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Blue, TEXT("DoPickUp"));

	EquipWeapon = weapon;

	bUseControllerRotationYaw = true;

	IWeaponInterface* InterfaceObj = Cast<IWeaponInterface>(weapon);

	if (InterfaceObj == nullptr)
		return;

	InterfaceObj->Execute_EventPickUp(weapon, this);
}

void AShootingGameCodeCharacter::DoDrop()
{
	bUseControllerRotationYaw = false;

	IWeaponInterface* InterfaceObj = Cast<IWeaponInterface>(EquipWeapon);

	if (InterfaceObj == nullptr)
		return;

	InterfaceObj->Execute_EventDrop(EquipWeapon, this);

	EquipWeapon = nullptr;
}

void AShootingGameCodeCharacter::BindPlayerState()
{
	AShootingPlayerState* ps = Cast<AShootingPlayerState>(GetPlayerState());
	if (IsValid(ps))
	{
		ps->Fuc_Dele_UpdateHp.AddDynamic(this, &AShootingGameCodeCharacter::OnUpdateHp);
		OnUpdateHp(ps->CurHp, ps->MaxHp);
		return;
	}

	FTimerManager& timerManager = GetWorld()->GetTimerManager();
	timerManager.SetTimer(th_BindPlayerState, this, &AShootingGameCodeCharacter::BindPlayerState, 0.1f, false);
}

void AShootingGameCodeCharacter::DoRevive()
{
	AShootingPlayerState* ps = Cast<AShootingPlayerState>(GetPlayerState());
	if (IsValid(ps) == false)
		return;

	ps->AddHeal(100.0f);

	ResRevive(GetRandomReviveTransform());
}

FTransform AShootingGameCodeCharacter::GetRandomReviveTransform()
{
	TArray<AActor*> arrPS;
	UGameplayStatics::GetAllActorsOfClass(GetWorld(), APlayerStart::StaticClass(), arrPS);
	AActor* randStart = arrPS[FMath::RandRange(0, arrPS.Num() - 1)];

	return randStart->GetActorTransform();
}

void AShootingGameCodeCharacter::CreateNameTag()
{
	check(NameTagClass);

	NameTagWidget = CreateWidget<UCustomUserWidget>(GetWorld(), NameTagClass);
	NameTagWidget->AddToViewport();
	NameTagWidget->OwnChar = this;

	FTimerManager& timerManager = GetWorld()->GetTimerManager();
	timerManager.SetTimer(th_NameTag, this, &AShootingGameCodeCharacter::EventUpdateNameTag, 0.01f, true);
}

void AShootingGameCodeCharacter::SpawnGrenade()
{
	if (UGameplayStatics::GetPlayerCharacter(GetWorld(), 0) != this)
		return;

	FVector StartPos;
	FVector Impluse;
	FVector CameraStart = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
	FVector CameraForward = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetActorForwardVector();

	StartPos = CameraStart + (CameraForward * 400.0f);
	FVector addUp = CameraForward + FVector(0.0f, 0.0f, 0.2f);
	Impluse = addUp.GetSafeNormal() + CameraForward * 550.0f;

	ReqSpawnGrenade(StartPos, Impluse);
}

void AShootingGameCodeCharacter::ShowGrenadeGuideLine()
{
	FVector StartPos;
	FVector Impluse;
	FVector CameraStart = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetCameraLocation();
	FVector CameraForward = GetWorld()->GetFirstPlayerController()->PlayerCameraManager->GetActorForwardVector();

	StartPos = CameraStart + (CameraForward * 400.0f);
	FVector addUp = CameraForward + FVector(0.0f, 0.0f, 0.2f);
	Impluse = addUp.GetSafeNormal() + CameraForward * 1000.0f;

	FPredictProjectilePathParams PathParams;
	FPredictProjectilePathResult PathResult;

	TArray<TEnumAsByte<EObjectTypeQuery>> ObjArr;
	ObjArr.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldStatic));
	ObjArr.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_WorldDynamic));
	ObjArr.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Pawn));
	ObjArr.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_PhysicsBody));
	ObjArr.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Vehicle));
	ObjArr.Add(UEngineTypes::ConvertToObjectType(ECollisionChannel::ECC_Destructible));

	PathParams.StartLocation = StartPos;
	PathParams.LaunchVelocity = Impluse;
	PathParams.ProjectileRadius = 16;
	PathParams.ObjectTypes = ObjArr;
	PathParams.DrawDebugType = EDrawDebugTrace::ForOneFrame;

	bool bHit = UGameplayStatics::PredictProjectilePath(GetWorld(), PathParams, PathResult);
}

FRotator AShootingGameCodeCharacter::GetPlayerRotation()
{
	ACharacter* pChar0 = UGameplayStatics::GetPlayerCharacter(GetWorld(), 0);
	if (pChar0 == this)
	{
		return GetControlRotation();
	}

	return PlayerRotation;
}

bool AShootingGameCodeCharacter::IsEquip()
{
	return IsValid(EquipWeapon);
}

//////////////////////////////////////////////////////////////////////////
// Input

void AShootingGameCodeCharacter::SetupPlayerInputComponent(class UInputComponent* PlayerInputComponent)
{
	// Set up action bindings
	if (UEnhancedInputComponent* EnhancedInputComponent = CastChecked<UEnhancedInputComponent>(PlayerInputComponent)) {
		
		//Jumping
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Triggered, this, &ACharacter::Jump);
		EnhancedInputComponent->BindAction(JumpAction, ETriggerEvent::Completed, this, &ACharacter::StopJumping);

		//Moving
		EnhancedInputComponent->BindAction(MoveAction, ETriggerEvent::Triggered, this, &AShootingGameCodeCharacter::Move);

		//Looking
		EnhancedInputComponent->BindAction(LookAction, ETriggerEvent::Triggered, this, &AShootingGameCodeCharacter::Look);

		//Trigger
		EnhancedInputComponent->BindAction(TriggerAction, ETriggerEvent::Started, this, &AShootingGameCodeCharacter::TriggerPress);
		EnhancedInputComponent->BindAction(TriggerAction, ETriggerEvent::Completed, this, &AShootingGameCodeCharacter::TriggerRelease);

		//Reload
		EnhancedInputComponent->BindAction(ReloadAction, ETriggerEvent::Started, this, &AShootingGameCodeCharacter::Reload);

		//PressF
		EnhancedInputComponent->BindAction(PressFAction, ETriggerEvent::Started, this, &AShootingGameCodeCharacter::PressF);

		//Drop
		EnhancedInputComponent->BindAction(DropAction, ETriggerEvent::Started, this, &AShootingGameCodeCharacter::Drop);

		//Grenade
		EnhancedInputComponent->BindAction(GrenadeAction, ETriggerEvent::Started, this, &AShootingGameCodeCharacter::GrenadePress);
		EnhancedInputComponent->BindAction(GrenadeAction, ETriggerEvent::Completed, this, &AShootingGameCodeCharacter::GrenadeRelease);

		//Test
		EnhancedInputComponent->BindAction(TestAction, ETriggerEvent::Started, this, &AShootingGameCodeCharacter::Test);
	}

}

void AShootingGameCodeCharacter::Move(const FInputActionValue& Value)
{
	if (IsRagdoll == true)
		return;

	// input is a Vector2D
	FVector2D MovementVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// find out which way is forward
		const FRotator Rotation = Controller->GetControlRotation();
		const FRotator YawRotation(0, Rotation.Yaw, 0);

		// get forward vector
		const FVector ForwardDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::X);
	
		// get right vector 
		const FVector RightDirection = FRotationMatrix(YawRotation).GetUnitAxis(EAxis::Y);

		// add movement 
		AddMovementInput(ForwardDirection, MovementVector.Y);
		AddMovementInput(RightDirection, MovementVector.X);
	}
}

void AShootingGameCodeCharacter::Look(const FInputActionValue& Value)
{
	// input is a Vector2D
	FVector2D LookAxisVector = Value.Get<FVector2D>();

	if (Controller != nullptr)
	{
		// add yaw and pitch input to controller
		AddControllerYawInput(LookAxisVector.X);
		AddControllerPitchInput(LookAxisVector.Y);
	}
}

void AShootingGameCodeCharacter::TriggerPress(const FInputActionValue& Value)
{
	ReqTrigger(true);
}

void AShootingGameCodeCharacter::TriggerRelease(const FInputActionValue& Value)
{
	ReqTrigger(false);
}

void AShootingGameCodeCharacter::Reload(const FInputActionValue& Value)
{
	ReqReload();
}

void AShootingGameCodeCharacter::PressF(const FInputActionValue& Value)
{
	//GEngine->AddOnScreenDebugMessage(-1, 15.0f, FColor::Blue, TEXT("PressF"));
	ReqPressF();
}

void AShootingGameCodeCharacter::Drop(const FInputActionValue& Value)
{
	ReqDrop();
}

void AShootingGameCodeCharacter::GrenadePress(const FInputActionValue& Value)
{
	FTimerManager& timerManager = GetWorld()->GetTimerManager();
	timerManager.SetTimer(th_Grenade, this, &AShootingGameCodeCharacter::ShowGrenadeGuideLine, 0.01f, true);
}

void AShootingGameCodeCharacter::GrenadeRelease(const FInputActionValue& Value)
{
	FTimerManager& timerManager = GetWorld()->GetTimerManager();
	timerManager.ClearTimer(th_Grenade);

	ReqGrenade();
}

void AShootingGameCodeCharacter::Test(const FInputActionValue& Value)
{
	FTimerManager& timermanager = GetWorld()->GetTimerManager();

	if (timermanager.TimerExists(th_Grenade) == false)
	{
		timermanager.SetTimer(th_Grenade, this, &AShootingGameCodeCharacter::ShowGrenadeGuideLine, 0.01, true);
		//DoGetUp();
	}
	else
	{
		timermanager.ClearTimer(th_Grenade);
		//DoRagdoll();
	}
}
