"""Body line counts of named handler functions at a git revision and in the working tree.

usage: metrics.py BASEREV
Prints per-family counts, medians and totals for the functions listed below.
"""
import re, subprocess, sys, statistics, os

base = sys.argv[1]
root = 'plugin/ue_mcp_bridge/Source/UE_MCP_Bridge/Private/Handlers/'

F4 = {
 'Gameplay/GameplayHandlers.cpp': ['CreateSmartObjectDefinition','CreateBlackboard','CreateBehaviorTree','CreateEqsQuery','CreateStateTree','CreateGameMode','CreateGameState','CreatePlayerController','CreatePlayerState','CreateHud','CreateBlueprintWithParent','CreateFrameworkBlueprint'],
 'Gas/GasHandlers.cpp': ['CreateGasBlueprint','CreateGameplayEffect','CreateGameplayAbility','CreateAttributeSet','CreateGameplayCue'],
 'Widget/WidgetHandlers.cpp': ['CreateWidgetBlueprint','CreateEditorUtilityWidget','CreateEditorUtilityBlueprint'],
 'Blueprint/BlueprintHandlers.cpp': ['CreateBlueprint'],
 'Blueprint/BlueprintHandlers_Functions.cpp': ['CreateBlueprintInterface'],
 'Animation/AnimationHandlers.cpp': ['CreateAnimBlueprint','CreateMontage','CreateBlendspace','CreateBlendspace1D'],
 'Animation/AnimationHandlers_Sequence.cpp': ['CreateSequence','CreateAnimComposite'],
 'Animation/AnimationHandlers_MotionMatching.cpp': ['CreatePoseSearchSchema','CreateMirrorDataTable','CreatePoseSearchNormalizationSet'],
 'Animation/AnimationHandlers_StateMachine.cpp': ['CreateIKRig','CreatePoseSearchDatabase'],
 'Asset/AssetHandlers.cpp': ['CreateDataAsset','CreateAssetByClass','CreateAssetOfClass','CreateInterchangePipeline'],
 'Asset/AssetHandlers_Import.cpp': ['CreateDataTable','CreateCurveTable','CreateStringTable'],
 'Asset/AssetHandlers_Enum.cpp': ['CreateUserDefinedEnum'],
 'Asset/AssetHandlers_Struct.cpp': ['CreateUserDefinedStruct'],
 'Reflection/ReflectionHandlers.cpp': ['CreateEnum'],
 'Audio/AudioHandlers.cpp': ['CreateSoundCue'],
 'Audio/AudioHandlers_Mixing.cpp': ['CreateSubmix','CreateSoundClass','CreateSoundMix','CreateConcurrency','CreateAttenuation'],
 'Audio/AudioHandlers_MetaSound.cpp': ['CreateMetaSoundSource'],
 'Foliage/FoliageHandlers.cpp': ['CreateFoliageType'],
 'Landscape/LandscapeHandlers.cpp': ['CreateLandscapeLayerInfo'],
 'PCG/PCGHandlers.cpp': ['CreatePCGGraph'],
 'Sequencer/SequencerHandlers.cpp': ['CreateLevelSequence'],
}
F6 = {
 'Level/LevelHandlers.cpp': ['PlaceActor','SpawnActorUnchecked','SpawnForPlacement','PlaceActorOfClass','AddComponentToActor','DeleteActor'],
 'Level/LevelHandlers_Lights.cpp': ['SpawnLight'],
 'Level/LevelHandlers_Volumes.cpp': ['SpawnVolume'],
 'Level/LevelHandlers_SkeletalMesh.cpp': ['SpawnSkeletalMeshActor'],
 'Audio/AudioHandlers.cpp': ['SpawnAmbientSound'],
 'Gameplay/GameplayHandlers.cpp': ['SpawnNavModifierVolume','AddStateTreeComponent','AddSmartObjectComponent','AddPerceptionComponent'],
 'PCG/PCGHandlers.cpp': ['SpawnPCGVolume'],
 'Gas/GasHandlers.cpp': ['AddAbilitySystemComponent'],
}

def text_at(rev, path):
    if rev is None:
        try:
            return open(root + path, encoding='utf-8').read().replace('\r\n', '\n')
        except FileNotFoundError:
            return ''
    r = subprocess.run(['git', 'show', f'{rev}:{root}{path}'], capture_output=True, text=True, encoding='utf-8')
    return r.stdout.replace('\r\n', '\n')

def body_len(src, name):
    m = re.search(r'\n[^\n]*(?:::|\s)' + re.escape(name) + r'\(const TSharedPtr<FJsonObject>& Params[^\n]*\)\n\{\n', src)
    if not m:
        m = re.search(r'\n[^\n]*::' + re.escape(name) + r'\([^\n]*\)\n\{\n', src)
    if not m:
        return None
    end = src.find('\n}\n', m.end())
    return src[m.end():end].count('\n') + 1

def family(name, table):
    before, after = [], []
    for path, fns in table.items():
        b, a = text_at(base, path), text_at(None, path)
        for fn in fns:
            lb, la = body_len(b, fn), body_len(a, fn)
            if lb: before.append(lb)
            if la: after.append(la)
    print(f'{name}: before n={len(before)} total={sum(before)} median={statistics.median(before) if before else 0}; '
          f'after n={len(after)} total={sum(after)} median={statistics.median(after) if after else 0}')

family('F4 creators', F4)
family('F6 spawns and adders', F6)
