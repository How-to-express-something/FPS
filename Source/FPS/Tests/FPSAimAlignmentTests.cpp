#include "../FPSAimAlignment.h"

#if WITH_DEV_AUTOMATION_TESTS
#include "Misc/AutomationTest.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FFPSAimAlignmentTest, "FPS.Aim.Alignment",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FFPSAimAlignmentTest::RunTest(const FString& Parameters)
{
	const FVector Rear(7.f, -12.f, 6.f);
	const FVector Front(42.f, 3.f, 11.f);
	const FVector Up(0.f, 0.f, 1.f);
	const FVector Scale(1.2f, 0.8f, 1.1f);
	const float EyeRelief = 15.f;
	FTransform Target;
	if (!TestTrue(TEXT("Valid sight geometry solves"),
		FPSAim::SolveSightAlignment(Rear, Front, Up, Scale, EyeRelief, Target)))
	{
		return false;
	}

	const FVector AlignedRear = Target.TransformPosition(Rear);
	const FVector AlignedFront = Target.TransformPosition(Front);
	TestTrue(TEXT("Rear lies on the centre ray at the requested eye distance"),
		AlignedRear.Equals(FVector(EyeRelief, 0.f, 0.f), 0.001f));
	TestTrue(TEXT("Front lies farther along the same centre ray"),
		FMath::Abs(AlignedFront.Y) < 0.001f && FMath::Abs(AlignedFront.Z) < 0.001f &&
		AlignedFront.X > AlignedRear.X);
	const FVector AlignedUp = Target.TransformVector(Up);
	TestTrue(TEXT("Up reference resolves roll towards camera up"),
		FMath::Abs(AlignedUp.Y) < 0.001f && AlignedUp.Z > 0.f);
	TestTrue(TEXT("Arms scale is preserved"), Target.GetScale3D().Equals(Scale));

	// Recover geometry through varied camera/arms poses, including already aligned
	// arms. The absolute target must not change as an ADS correction is reapplied.
	const FTransform Camera(FRotator(-35.f, 123.f, 4.f), FVector(1200.f, -300.f, 90.f));
	for (const float Alpha : {0.f, 0.2f, 0.7f, 1.f, 1.f, 0.4f, 0.f})
	{
		const FTransform Current(
			FQuat::Slerp(FRotator(4.f, -25.f, 9.f).Quaternion(), Target.GetRotation(), Alpha),
			FMath::Lerp(FVector(5.f, -8.f, -20.f), Target.GetLocation(), Alpha), Scale);
		const FTransform ArmsWorld = Current * Camera;
		FTransform Solved;
		TestTrue(TEXT("Geometry recovered after arbitrary current correction solves"),
			FPSAim::SolveSightAlignment(
				ArmsWorld.InverseTransformPosition(ArmsWorld.TransformPosition(Rear)),
				ArmsWorld.InverseTransformPosition(ArmsWorld.TransformPosition(Front)),
				ArmsWorld.InverseTransformVector(ArmsWorld.TransformVector(Up)),
				Scale, EyeRelief, Solved));
		TestTrue(TEXT("Target is independent of current blend and camera pose"), Solved.Equals(Target, 0.001f));
	}

	FTransform Invalid;
	TestFalse(TEXT("Coincident sights are rejected"),
		FPSAim::SolveSightAlignment(Rear, Rear, Up, Scale, EyeRelief, Invalid));
	TestFalse(TEXT("Parallel up reference is rejected"),
		FPSAim::SolveSightAlignment(Rear, Front, Front - Rear, Scale, EyeRelief, Invalid));
	TestFalse(TEXT("Zero up reference is rejected"),
		FPSAim::SolveSightAlignment(Rear, Front, FVector::ZeroVector, Scale, EyeRelief, Invalid));
	TestFalse(TEXT("Negative eye relief is rejected"),
		FPSAim::SolveSightAlignment(Rear, Front, Up, Scale, -1.f, Invalid));
	TestFalse(TEXT("Zero scale is rejected"),
		FPSAim::SolveSightAlignment(Rear, Front, Up, FVector(1.f, 0.f, 1.f), EyeRelief, Invalid));
	return true;
}
#endif
