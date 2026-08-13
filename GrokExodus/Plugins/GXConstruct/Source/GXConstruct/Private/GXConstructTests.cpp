// Copyright Grok Exodus. All Rights Reserved.

#include "Misc/AutomationTest.h"
#include "GXItemTypes.h"
#include "GXRecipe.h"
#include "GXBlockDef.h"

IMPLEMENT_SIMPLE_AUTOMATION_TEST(FGXConstructTypesSmoke, "GX.Construct.TypesSmoke",
	EAutomationTestFlags::EditorContext | EAutomationTestFlags::EngineFilter)

bool FGXConstructTypesSmoke::RunTest(const FString& Parameters)
{
	FGXItemStack Stack;
	Stack.ItemId = TEXT("IronOre");
	Stack.Count = 50;
	TestFalse(TEXT("stack not empty"), Stack.IsEmpty());
	TestEqual(TEXT("mass"), Stack.GetMassKg(0.1f), 5.0f);

	FGXRecipe R;
	R.RecipeId = TEXT("IronIngot");
	R.Machine = EGXMachineKind::Refinery;
	FGXRecipeIO In;
	In.ItemId = TEXT("IronOre");
	In.Count = 2;
	R.Inputs.Add(In);
	FGXRecipeIO Out;
	Out.ItemId = TEXT("IronIngot");
	Out.Count = 1;
	R.Outputs.Add(Out);
	TestEqual(TEXT("conservation shape"), R.Inputs.Num() + R.Outputs.Num(), 3);

	TestEqual(TEXT("large cell"), UGXBlockDef::CellSizeMeters(EGXGridClass::Large), 2.5f);
	TestEqual(TEXT("small cell"), UGXBlockDef::CellSizeMeters(EGXGridClass::Small), 0.5f);
	return true;
}
