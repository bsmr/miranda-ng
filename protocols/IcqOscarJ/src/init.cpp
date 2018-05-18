// ---------------------------------------------------------------------------80
//                ICQ plugin for Miranda Instant Messenger
//                ________________________________________
//
// Copyright © 2000-2001 Richard Hughes, Roland Rabien, Tristan Van de Vreede
// Copyright © 2001-2002 Jon Keating, Richard Hughes
// Copyright © 2002-2004 Martin Öberg, Sam Kothari, Robert Rainwater
// Copyright © 2004-2010 Joe Kucera
// Copyright © 2012-2018 Miranda NG team
//
// This program is free software; you can redistribute it and/or
// modify it under the terms of the GNU General Public License
// as published by the Free Software Foundation; either version 2
// of the License, or (at your option) any later version.
//
// This program is distributed in the hope that it will be useful,
// but WITHOUT ANY WARRANTY; without even the implied warranty of
// MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
// GNU General Public License for more details.
//
// You should have received a copy of the GNU General Public License
// along with this program; if not, write to the Free Software
// Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA 02110-1301 USA.
// -----------------------------------------------------------------------------

#include "stdafx.h"

#include "m_extraicons.h"
#include "m_icolib.h"

int &hLangpack(g_plugin.m_hLang);

BOOL bPopupService = FALSE;

HANDLE hExtraXStatus;

PLUGININFOEX pluginInfo = {
	sizeof(PLUGININFOEX),
	__PLUGIN_NAME,
	PLUGIN_MAKE_VERSION(__MAJOR_VERSION, __MINOR_VERSION, __RELEASE_NUM, __BUILD_NUM),
	__DESCRIPTION,
	__AUTHOR,
	__COPYRIGHT,
	__AUTHORWEB,
	UNICODE_AWARE,   //doesn't replace anything built-in
	{ 0x73a9615c, 0x7d4e, 0x4555, { 0xba, 0xdb, 0xee, 0x5, 0xdc, 0x92, 0x8e, 0xff } } // {73A9615C-7D4E-4555-BADB-EE05DC928EFF}
};

extern "C" PLUGININFOEX __declspec(dllexport) *MirandaPluginInfoEx(DWORD)
{
	return &pluginInfo;
}

/////////////////////////////////////////////////////////////////////////////////////////

extern "C" __declspec(dllexport) const MUUID MirandaInterfaces[] = { MIID_PROTOCOL, MIID_LAST };

/////////////////////////////////////////////////////////////////////////////////////////

CMPlugin g_plugin;

/////////////////////////////////////////////////////////////////////////////////////////

int ModuleLoad(WPARAM, LPARAM)
{
	bPopupService = ServiceExists(MS_POPUP_ADDPOPUPT);
	return 0;
}

IconItem iconList[] =
{
	{ LPGEN("Expand string edit"), "ICO_EXPANDSTRINGEDIT", IDI_EXPANDSTRINGEDIT }
};

extern "C" int __declspec(dllexport) Load(void)
{
	mir_getLP(&pluginInfo);

	srand(time(0));
	_tzset();

	// Initialize charset conversion routines
	InitI18N();

	// Register static services
	CreateServiceFunction(ICQ_DB_GETEVENTTEXT_MISSEDMESSAGE, icq_getEventTextMissedMessage);

	// Init extra statuses
	InitXStatusIcons();
	HookEvent(ME_SKIN2_ICONSCHANGED, OnReloadIcons);

	HookEvent(ME_SYSTEM_MODULELOAD, ModuleLoad);
	HookEvent(ME_SYSTEM_MODULEUNLOAD, ModuleLoad);

	hExtraXStatus = ExtraIcon_RegisterIcolib("xstatus", LPGEN("ICQ xStatus"), "icq_xstatus13");

	g_plugin.registerIcon("ICQ", iconList);

	g_MenuInit();
	return 0;
}

extern "C" int __declspec(dllexport) Unload(void)
{
	// destroying contact menu
	g_MenuUninit();
	return 0;
}

/////////////////////////////////////////////////////////////////////////////////////////
// UpdateGlobalSettings event

void CIcqProto::UpdateGlobalSettings()
{
	char szServer[MAX_PATH] = "";
	getSettingStringStatic(NULL, "OscarServer", szServer, MAX_PATH);

	m_bSecureConnection = getByte("SecureConnection", DEFAULT_SECURE_CONNECTION);
	if (szServer[0]) {
		if (strstr(szServer, "aol.com"))
			setString("OscarServer", m_bSecureConnection ? DEFAULT_SERVER_HOST_SSL : DEFAULT_SERVER_HOST);

		if (m_bSecureConnection && !_strnicmp(szServer, "login.", 6)) {
			setString("OscarServer", DEFAULT_SERVER_HOST_SSL);
			setWord("OscarPort", DEFAULT_SERVER_PORT_SSL);
		}
	}

	if (m_hNetlibUser) {
		NETLIBUSERSETTINGS nlus = { sizeof(NETLIBUSERSETTINGS) };
		if (!m_bSecureConnection && Netlib_GetUserSettings(m_hNetlibUser, &nlus)) {
			if (nlus.useProxy && nlus.proxyType == PROXYTYPE_HTTP)
				m_bGatewayMode = 1;
			else
				m_bGatewayMode = 0;
		}
		else m_bGatewayMode = 0;
	}

	m_bSecureLogin = getByte("SecureLogin", DEFAULT_SECURE_LOGIN);
	m_bLegacyFix = getByte("LegacyFix", DEFAULT_LEGACY_FIX);
	m_wAnsiCodepage = getWord("AnsiCodePage", DEFAULT_ANSI_CODEPAGE);
	m_bDCMsgEnabled = getByte("DirectMessaging", DEFAULT_DCMSG_ENABLED);
	m_bTempVisListEnabled = getByte("TempVisListEnabled", DEFAULT_TEMPVIS_ENABLED);
	m_bSsiEnabled = getByte("UseServerCList", DEFAULT_SS_ENABLED);
	m_bSsiSimpleGroups = FALSE; /// TODO: enable, after server-list revolution is over
	m_bAvatarsEnabled = getByte("AvatarsEnabled", DEFAULT_AVATARS_ENABLED);
	m_bXStatusEnabled = getByte("XStatusEnabled", DEFAULT_XSTATUS_ENABLED);
	m_bMoodsEnabled = getByte("MoodsEnabled", DEFAULT_MOODS_ENABLED);
}
