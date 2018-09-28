#include "extension.h"
#include <igameevents.h>
#include "CDetour/detours.h"
#define GAMEDATA_FILE "l4d2_bugfixes"

CDetour *hg_witch1	= NULL;
CDetour *hg_witch2	= NULL;
CDetour *hg_charge1	= NULL;

IGameConfig *hg_conf = NULL;
IServerGameEnts *gameents = NULL;

float fg_time[L4D_MAX_PLAYERS+1];
int ig_offset = -1;
char *sg_addr;

BYTE patch_buf_org[6];
BYTE patch_buf_new[] = "\x90\x90\x90\x90\x90\x90";

BugFixes g_BugFixes;
SMEXT_LINK(&g_BugFixes);

void BugFixes::ChargerImpactPatch(bool enable)
{
	if (sg_addr)
	{
		if (enable)
		{
			memcpy(sg_addr, &patch_buf_new, 6);
		}
		else
		{
			memcpy(sg_addr, &patch_buf_org, 6);
		}
	}
}

DETOUR_DECL_MEMBER1(HxWitch1, void*, CBaseEntity*, pEntity)
{
	void *result = DETOUR_MEMBER_CALL(HxWitch1)(pEntity);
	DWORD *CharId = ((DWORD *)this + ig_offset);
	*CharId = 8;

	return result;
}

DETOUR_DECL_MEMBER0(HxWitch2, void*)
{
	void *result = DETOUR_MEMBER_CALL(HxWitch2)();
	DWORD *CharId = ((DWORD *)this + ig_offset);
	*CharId = 8;

	return result;
}

DETOUR_DECL_MEMBER5(HxCharge1, int, CBaseEntity *, pEntity, Vector  const&, v1, Vector  const&, v2, CGameTrace *, gametrace, void *, movedata)
{
	int target = gamehelpers->IndexOfEdict(gameents->BaseEntityToEdict((CBaseEntity*)pEntity));
	if (target > 0 && target <= L4D_MAX_PLAYERS)
	{
		float fTime = Plat_FloatTime();
		if (fTime - fg_time[target] > 1.0)
		{
			fg_time[target] = fTime;
			BugFixes::ChargerImpactPatch(true);
			int result = DETOUR_MEMBER_CALL(HxCharge1)(pEntity,v1,v2,gametrace,movedata);
			BugFixes::ChargerImpactPatch(false);
			return result;
		}
	}

	return DETOUR_MEMBER_CALL(HxCharge1)(pEntity,v1,v2,gametrace,movedata);
}

void BugFixes::RemoveHooks()
{
	ChargerImpactPatch(false);

	if (hg_witch1)
	{
		hg_witch1->Destroy();
		hg_witch1 = NULL;
	}
	if (hg_witch2)
	{
		hg_witch2->Destroy();
		hg_witch2 = NULL;
	}
	if (hg_charge1)
	{
		hg_charge1->Destroy();
		hg_charge1 = NULL;
	}
}

bool BugFixes::SetupHooks()
{
	CDetourManager::Init(g_pSM->GetScriptingEngine(), hg_conf);

	hg_witch1	= DETOUR_CREATE_MEMBER(HxWitch1, "WitchAttack::WitchAttack");
	hg_witch2	= DETOUR_CREATE_MEMBER(HxWitch2, "WitchAttack::GetVictim");
	hg_charge1	= DETOUR_CREATE_MEMBER(HxCharge1, "CCharge::HandleCustomCollision");

	if (hg_witch1 && hg_witch2 && hg_charge1)
	{
		hg_witch1->EnableDetour();
		hg_witch2->EnableDetour();
		hg_charge1->EnableDetour();
		return true;
	}

	RemoveHooks();
	return false;
}

bool BugFixes::SDK_OnMetamodLoad( ISmmAPI *ismm, char *error, size_t maxlength, bool late )
{
	size_t maxlen = maxlength;
	GET_V_IFACE_ANY(GetServerFactory, gameents, IServerGameEnts, INTERFACEVERSION_SERVERGAMEENTS);
	return true;
}

bool BugFixes::SDK_OnLoad( char *error, size_t maxlength, bool late )
{
	char sError[255];
	if (!gameconfs->LoadGameConfigFile(GAMEDATA_FILE, &hg_conf, sError, sizeof(sError)-1))
	{
		if (sError[0])
		{
			snprintf(error, maxlength, "Проблема с BugFixes.txt: %s", sError);
		}
		return false;
	}

	hg_conf->GetOffset("WitchAttackCharaster", &ig_offset);
	hg_conf->GetMemSig("CCharge::HandleCustomCollision_code", (void **)&sg_addr);

	if (sg_addr == NULL)
	{
		snprintf(error, maxlength, "Error CCharge::HandleCustomCollision");
		return false;
	}

	SetMemPatchable(sg_addr, 6); 
	memcpy(&patch_buf_org, sg_addr, 6);

	if (!SetupHooks())
	{
		snprintf(error, maxlength, "Проблема с Offset");
		return false;
	}

	return true;
}

void BugFixes::SDK_OnUnload()
{
	RemoveHooks();
	gameconfs->CloseGameConfigFile(hg_conf);
}
