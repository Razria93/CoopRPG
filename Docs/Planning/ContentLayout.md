# CoopRPG 에셋 분류와 첫 이동표

2026-11-21 · feat/expedition-flow

## 분류 규칙

프로젝트 제작물은 대상별로 묶고 그 안에서 형식을 구분한다. 적은 Characters/Enemies/<종류> 아래에 배치용 Blueprints, 이동용 Animation/BlendSpaces, 단발 동작용 Animation/Montages를 둔다. 외부 팩 작업 사본에는 ThirdParty 아래에 같은 소유 구조를 적용한다. 원본은 별도로 보관한다. 둘 이상이 참조하는 자산만 공통 경로에 둔다.

| 조사 대상 | 관찰한 구조 | 적용 |
|---|---|---|
| Stellar | 01_Character, 03_Animation, 04_Montage, 05_BlendSpace와 보스별 Animation·Blueprints·Materials가 공존한다. CAction과 CReaction이 Montage를 재생한다. | 배치 BP, 이동 Blend Space, 단발 액션·리액션 Montage의 책임을 가져온다. |
| CoopRPG | 히어로별로 개별 이동 Blend Space가 있다. 이전 전에는 ExpeditionSkirmish에 적·아이템·포탈의 작업 사본이 섞여 있었다. | 대상별 경로를 확장한다. 전투와 드롭은 전용 서버가 확정한다. |

상자·히어로 공급 팩, 환경, 입력과 기존 공용 재질은 기존 경로에서 유지한다. 후속 이동은 실제 사용자를 조사한 뒤 결정한다.

### 폴더와 이름을 고르는 순서

1. 배치하거나 장착하는 대상을 정한다. 캐릭터는 `Characters/Heroes`와 `Characters/Enemies`, 획득물은 `Items`, 포탈은 `Environment/Portal`, 여러 전리품에 쓰는 효과는 `Effects/Loot`가 소유한다. 텍스처와 머티리얼 이름이 같아도 참조와 출처가 같은지 먼저 확인한다.
2. 대상 안에서 `Blueprints`, `Meshes`(스켈레톤·Physics Asset 포함), `Materials`, `Textures`, `VFX`, `Animation/{Sequences,BlendSpaces,Montages}`로 나눈다. 이미 한 형식만 있는 작은 폴더는 필요해질 때 나눈다. 작성물 이름은 `BP_Enemy_종류`, `BS_종류_Movement`, `AM_종류_동작`을 사용한다. 이동한 외부 사본의 자산명은 원래 이름을 유지한다.
3. 적처럼 종류별로 외형·장비·행동을 조정하는 객체는 네이티브 부모를 가진 BP로 배치한다. 바닥이나 바위처럼 재사용할 동작이 없는 배경은 Static Mesh Actor로 배치할 수 있다. BP의 Details에서 외형과 소켓을 편집하고 서버가 판정과 복제 시점을 소유한다.
4. 외부 원본을 별도 임포트 프로젝트에 보관한다. 작업 사본은 에디터에서 옮긴 뒤 새 에디터 프로세스에서 이전 경로·리다이렉터, 머티리얼·스켈레톤 참조를 검사한다. 문자열 로드 경로와 쿠킹 설정도 확인한다.

스텔라 대조 경로는 `Portfolio/Content/01_Character`, `03_Animation`, `04_Montage`, `05_BlendSpace`, `Source/Portfolio/Action/CAction.cpp`, `Reaction/CReaction.cpp`다. CoopRPG에서는 [공용 이동 AnimInstance](../../Game/Source/CoopRPG/Character/CoopRPGLocomotionAnimInstance.cpp)와 [적 부모](../../Game/Source/CoopRPG/Character/CoopRPGEnemy.cpp)가 해당 책임을 맡는다.

### 비공개 작업 사본 복원

로컬 검증 폴더 `Artifacts/Validation/enemy-asset-layout/`에 `enemy-layout-private.zip`과 `source-and-working-hashes.json`을 보관했다. 새 작업 트리에 원래 사용하던 히어로·환경·상자 사본과 함께 이 ZIP의 `Game/Content/ThirdParty/` 트리를 복원한다. ZIP에는 이번에 옮긴 77개와 원본에서 추가한 BlackKnight 피격·사망 시퀀스 두 개가 들어 있다. 복원 경로와 파일별 SHA-256은 매니페스트와 대조한다. 프로젝트 제작 BP·Blend Space·Montage는 `Game/Content/Characters/Enemies/`에서 관리한다.

## 첫 이동표

두 경로의 공통 접두어는 `/Game/ThirdParty/`다. 참조 열은 이동 전 Asset Registry에서 읽었다. 문자열 경로는 Enemy, EnemyLoot, ExtractionZone 및 DefaultGame.ini에서 별도로 대조한다.

| 기존 패키지 | 새 패키지 | 참조 패키지 |
|---|---|---|
| `BlackKnight/BlackKnight_Attack01_ANIM` | `Characters/Enemies/BlackKnight/Animation/Sequences/BlackKnight_Attack01_ANIM` | 상위 에셋 또는 직접 로드 |
| `BlackKnight/BlackKnight_IdleDefense_ANIM` | `Characters/Enemies/BlackKnight/Animation/Sequences/BlackKnight_IdleDefense_ANIM` | 상위 에셋 또는 직접 로드 |
| `BlackKnight/BlackKnight_IdleNormal_ANIM` | `Characters/Enemies/BlackKnight/Animation/Sequences/BlackKnight_IdleNormal_ANIM` | 상위 에셋 또는 직접 로드 |
| `BlackKnight/BlackKnight_WalkFWD_ANIM` | `Characters/Enemies/BlackKnight/Animation/Sequences/BlackKnight_WalkFWD_ANIM` | 상위 에셋 또는 직접 로드 |
| `BlackKnight/DefaultPBR_MAT` | `Characters/Enemies/BlackKnight/Materials/DefaultPBR_MAT` | Axe_SM, BlackKnight_SK, Shield_SM |
| `BlackKnight/Axe_SM` | `Characters/Enemies/BlackKnight/Meshes/Axe_SM` | 상위 에셋 또는 직접 로드 |
| `BlackKnight/BlackKnight_PhysicsAsset` | `Characters/Enemies/BlackKnight/Meshes/BlackKnight_PhysicsAsset` | BlackKnight_SK |
| `BlackKnight/BlackKnight_SK` | `Characters/Enemies/BlackKnight/Meshes/BlackKnight_SK` | BlackKnight_PhysicsAsset |
| `BlackKnight/BlackKnight_Skeleton` | `Characters/Enemies/BlackKnight/Meshes/BlackKnight_Skeleton` | BlackKnight_Attack01_ANIM, BlackKnight_IdleDefense_ANIM, BlackKnight_IdleNormal_ANIM, BlackKnight_SK, BlackKnight_WalkFWD_ANIM |
| `BlackKnight/Shield_SM` | `Characters/Enemies/BlackKnight/Meshes/Shield_SM` | 상위 에셋 또는 직접 로드 |
| `BlackKnight/Emissive_TEX` | `Characters/Enemies/BlackKnight/Textures/Emissive_TEX` | DefaultPBR_MAT |
| `BlackKnight/PBR_Default_BaseColor_TEX` | `Characters/Enemies/BlackKnight/Textures/PBR_Default_BaseColor_TEX` | DefaultPBR_MAT |
| `BlackKnight/RAM_TEX` | `Characters/Enemies/BlackKnight/Textures/RAM_TEX` | DefaultPBR_MAT |
| `ExpeditionSkirmish/Mushroom_Attack01Angry_ANIM` | `Characters/Enemies/Mushroom/Animation/Sequences/Mushroom_Attack01Angry_ANIM` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/Mushroom_DieAngry_ANIM` | `Characters/Enemies/Mushroom/Animation/Sequences/Mushroom_DieAngry_ANIM` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/Mushroom_GetHitAngry_ANIM` | `Characters/Enemies/Mushroom/Animation/Sequences/Mushroom_GetHitAngry_ANIM` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/Mushroom_IdleNormalAngry_ANIM` | `Characters/Enemies/Mushroom/Animation/Sequences/Mushroom_IdleNormalAngry_ANIM` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/Mushroom_walkFWDAngry_ANIM` | `Characters/Enemies/Mushroom/Animation/Sequences/Mushroom_walkFWDAngry_ANIM` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/DefaultPBR_MAT` | `Characters/Enemies/Mushroom/Materials/DefaultPBR_MAT` | Mushroom_SK |
| `ExpeditionSkirmish/Mushroom_PhysicsAsset` | `Characters/Enemies/Mushroom/Meshes/Mushroom_PhysicsAsset` | Mushroom_SK |
| `ExpeditionSkirmish/Mushroom_SK` | `Characters/Enemies/Mushroom/Meshes/Mushroom_SK` | Mushroom_PhysicsAsset |
| `ExpeditionSkirmish/Mushroom_Skeleton` | `Characters/Enemies/Mushroom/Meshes/Mushroom_Skeleton` | Mushroom_Attack01Angry_ANIM, Mushroom_DieAngry_ANIM, Mushroom_GetHitAngry_ANIM, Mushroom_IdleNormalAngry_ANIM, Mushroom_SK, Mushroom_walkFWDAngry_ANIM |
| `ExpeditionSkirmish/Emissive_TEX` | `Characters/Enemies/Mushroom/Textures/Emissive_TEX` | DefaultPBR_MAT |
| `ExpeditionSkirmish/PBR_Default_BaseColor_TEX` | `Characters/Enemies/Mushroom/Textures/PBR_Default_BaseColor_TEX` | DefaultPBR_MAT |
| `ExpeditionSkirmish/RAM_TEX` | `Characters/Enemies/Mushroom/Textures/RAM_TEX` | DefaultPBR_MAT |
| `ExpeditionSkirmish/MI_CoreFlame` | `Effects/Loot/Materials/MI_CoreFlame` | NS_LootDrop_Rare |
| `ExpeditionSkirmish/MI_LootDrop_Beam_Showcase` | `Effects/Loot/Materials/MI_LootDrop_Beam_Showcase` | NS_LootDrop_Rare, NS_LootDrop_Uncommon |
| `ExpeditionSkirmish/M_FlipbookPrimary` | `Effects/Loot/Materials/M_FlipbookPrimary` | MI_CoreFlame |
| `ExpeditionSkirmish/M_LootBeam` | `Effects/Loot/Materials/M_LootBeam` | MI_LootDrop_Beam_Showcase |
| `ExpeditionSkirmish/T_ArcaneCircleFlipbook` | `Effects/Loot/Textures/T_ArcaneCircleFlipbook` | M_FlipbookPrimary |
| `ExpeditionSkirmish/T_LootBeam` | `Effects/Loot/Textures/T_LootBeam` | M_LootBeam |
| `ExpeditionSkirmish/T_OrbitingRingsFlipbook` | `Effects/Loot/Textures/T_OrbitingRingsFlipbook` | MI_CoreFlame |
| `ExpeditionSkirmish/NS_LootDrop_Rare` | `Effects/Loot/VFX/NS_LootDrop_Rare` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/NS_LootDrop_Uncommon` | `Effects/Loot/VFX/NS_LootDrop_Uncommon` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/MI_Cylinder_01` | `Environment/Portal/Materials/MI_Cylinder_01` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/MI_Cylinder_03` | `Environment/Portal/Materials/MI_Cylinder_03` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/MI_EmmisiveColor` | `Environment/Portal/Materials/MI_EmmisiveColor` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/MI_RadialCircl` | `Environment/Portal/Materials/MI_RadialCircl` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/MI_WhiteCircle7` | `Environment/Portal/Materials/MI_WhiteCircle7` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/M_Circle` | `Environment/Portal/Materials/M_Circle` | MI_WhiteCircle7 |
| `ExpeditionSkirmish/M_DistortionNoise` | `Environment/Portal/Materials/M_DistortionNoise` | MI_Cylinder_01, MI_Cylinder_03 |
| `ExpeditionSkirmish/M_EmmisiveColor` | `Environment/Portal/Materials/M_EmmisiveColor` | MI_EmmisiveColor |
| `ExpeditionSkirmish/M_RadialCircle` | `Environment/Portal/Materials/M_RadialCircle` | MI_RadialCircl |
| `ExpeditionSkirmish/SM_Circle` | `Environment/Portal/Meshes/SM_Circle` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/SM_Cylinder` | `Environment/Portal/Meshes/SM_Cylinder` | NS_MagicCircle_19 |
| `ExpeditionSkirmish/T_CausticNoise_1` | `Environment/Portal/Textures/T_CausticNoise_1` | M_DistortionNoise |
| `ExpeditionSkirmish/T_Circle_02` | `Environment/Portal/Textures/T_Circle_02` | M_Circle |
| `ExpeditionSkirmish/T_Circle_04` | `Environment/Portal/Textures/T_Circle_04` | M_Circle |
| `ExpeditionSkirmish/T_Circle_14` | `Environment/Portal/Textures/T_Circle_14` | MI_WhiteCircle7 |
| `ExpeditionSkirmish/T_CloudNoise_1` | `Environment/Portal/Textures/T_CloudNoise_1` | MI_Cylinder_03 |
| `ExpeditionSkirmish/T_FirePilar` | `Environment/Portal/Textures/T_FirePilar` | M_DistortionNoise |
| `ExpeditionSkirmish/T_MarbleNoise_1` | `Environment/Portal/Textures/T_MarbleNoise_1` | MI_Cylinder_01, MI_WhiteCircle7, M_Circle |
| `ExpeditionSkirmish/T_NoiseLight_02` | `Environment/Portal/Textures/T_NoiseLight_02` | MI_Cylinder_01, MI_Cylinder_03 |
| `ExpeditionSkirmish/T_PerlinNoise_1` | `Environment/Portal/Textures/T_PerlinNoise_1` | MI_Cylinder_01, MI_Cylinder_03, M_DistortionNoise |
| `ExpeditionSkirmish/NS_MagicCircle_19` | `Environment/Portal/VFX/NS_MagicCircle_19` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/MI_3DAL_PlatformerExp_Set2` | `Items/MagicStone/Materials/MI_3DAL_PlatformerExp_Set2` | SM_MagicRock_V1 |
| `ExpeditionSkirmish/SM_MagicRock_V1` | `Items/MagicStone/Meshes/SM_MagicRock_V1` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/T_PlatformerExp_2_Basecolor` | `Items/MagicStone/Textures/T_PlatformerExp_2_Basecolor` | MI_3DAL_PlatformerExp_Set2 |
| `ExpeditionSkirmish/T_PlatformerExp_2_emission` | `Items/MagicStone/Textures/T_PlatformerExp_2_emission` | MI_3DAL_PlatformerExp_Set2 |
| `ExpeditionSkirmish/T_PlatformerExp_2_metallic` | `Items/MagicStone/Textures/T_PlatformerExp_2_metallic` | MI_3DAL_PlatformerExp_Set2 |
| `ExpeditionSkirmish/T_PlatformerExp_2_normal` | `Items/MagicStone/Textures/T_PlatformerExp_2_normal` | MI_3DAL_PlatformerExp_Set2 |
| `ExpeditionSkirmish/T_PlatformerExp_2_roughness` | `Items/MagicStone/Textures/T_PlatformerExp_2_roughness` | MI_3DAL_PlatformerExp_Set2 |
| `ExpeditionSkirmish/MI_3DAL_PlatformerExp_Set1` | `Items/Potion/Materials/MI_3DAL_PlatformerExp_Set1` | SM_Potion_V1 |
| `ExpeditionSkirmish/MI_3DAL_PlatformerExp_Set3` | `Items/Potion/Materials/MI_3DAL_PlatformerExp_Set3` | SM_LargePotion_V2 |
| `ExpeditionSkirmish/M_3DAL_PlatformerExp_Vol1` | `Items/Potion/Materials/M_3DAL_PlatformerExp_Vol1` | MI_3DAL_PlatformerExp_Set1 |
| `ExpeditionSkirmish/SM_LargePotion_V2` | `Items/Potion/Meshes/SM_LargePotion_V2` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/SM_Potion_V1` | `Items/Potion/Meshes/SM_Potion_V1` | 상위 에셋 또는 직접 로드 |
| `ExpeditionSkirmish/T_PlatformerExp_1_Basecolor` | `Items/Potion/Textures/T_PlatformerExp_1_Basecolor` | M_3DAL_PlatformerExp_Vol1 |
| `ExpeditionSkirmish/T_PlatformerExp_1_emission` | `Items/Potion/Textures/T_PlatformerExp_1_emission` | M_3DAL_PlatformerExp_Vol1 |
| `ExpeditionSkirmish/T_PlatformerExp_1_metallic` | `Items/Potion/Textures/T_PlatformerExp_1_metallic` | M_3DAL_PlatformerExp_Vol1 |
| `ExpeditionSkirmish/T_PlatformerExp_1_normal` | `Items/Potion/Textures/T_PlatformerExp_1_normal` | M_3DAL_PlatformerExp_Vol1 |
| `ExpeditionSkirmish/T_PlatformerExp_1_roughness` | `Items/Potion/Textures/T_PlatformerExp_1_roughness` | M_3DAL_PlatformerExp_Vol1 |
| `ExpeditionSkirmish/T_PlatformerExp_3_Basecolor` | `Items/Potion/Textures/T_PlatformerExp_3_Basecolor` | MI_3DAL_PlatformerExp_Set3 |
| `ExpeditionSkirmish/T_PlatformerExp_3_emission` | `Items/Potion/Textures/T_PlatformerExp_3_emission` | MI_3DAL_PlatformerExp_Set3 |
| `ExpeditionSkirmish/T_PlatformerExp_3_metallic` | `Items/Potion/Textures/T_PlatformerExp_3_metallic` | MI_3DAL_PlatformerExp_Set3 |
| `ExpeditionSkirmish/T_PlatformerExp_3_normal` | `Items/Potion/Textures/T_PlatformerExp_3_normal` | MI_3DAL_PlatformerExp_Set3 |
| `ExpeditionSkirmish/T_PlatformerExp_3_roughness` | `Items/Potion/Textures/T_PlatformerExp_3_roughness` | MI_3DAL_PlatformerExp_Set3 |
