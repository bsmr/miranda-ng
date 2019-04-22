/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (C) 2012-19 Miranda NG team (https://miranda-ng.org)
all portions of this codebase are copyrighted to the people
listed in contributors.txt.

This program is free software; you can redistribute it and/or
modify it under the terms of the GNU General Public License
as published by the Free Software Foundation; either version 2
of the License, or (at your option) any later version.

This program is distributed in the hope that it will be useful,
but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
GNU General Public License for more details.

You should have received a copy of the GNU General Public License
along with this program; if not, write to the Free Software
Foundation, Inc., 59 Temple Place - Suite 330, Boston, MA  02111-1307, USA.
*/

#include "stdafx.h"

#define print_enter printf("%s:%s:%d enter\n", __FILE__, __FUNCTION__, __LINE__)
#define print_exit_ok printf("%s:%s:%d exit ok\n", __FILE__, __FUNCTION__, __LINE__)
#define print_exit_error printf("\n\n\t%s:%s:%d exit error\n\n\n", __FILE__, __FUNCTION__, __LINE__)

int CDbxMDBX::InitModules()
{
	print_enter;
	txn_ptr_ro trnlck(m_txn_ro);
	cursor_ptr_ro cursor(m_curModules);
	
	MDBX_val key, data;
	while (mdbx_cursor_get(cursor, &key, &data, MDBX_NEXT) == MDBX_SUCCESS) {
		uint32_t iMod = *(uint32_t*)key.iov_base;
		const char *szMod = (const char*)data.iov_base;
		m_Modules[iMod] = szMod;
	}
	print_exit_ok;
	return 0;
}

// will create the offset if it needs to
uint32_t CDbxMDBX::GetModuleID(const char *szName)
{
	print_enter;
	if (szName == nullptr)
		return 0;

	uint32_t iHash = mir_hashstr(szName);
	if (m_Modules.find(iHash) == m_Modules.end()) {
		MDBX_val key = { &iHash, sizeof(iHash) }, data = { (void*)szName, strlen(szName) + 1 };
		{
			txn_ptr trnlck(StartTran());
			if (mdbx_put(trnlck, m_dbModules, &key, &data, 0) != MDBX_SUCCESS)
			{
				print_exit_error;
				return -1;
			}
			if (trnlck.commit() != MDBX_SUCCESS)
			{
				print_exit_error;
				return -1;
			}
		}

		m_Modules[iHash] = szName;
		DBFlush();
	}
	print_exit_ok;
	return iHash;
}

char* CDbxMDBX::GetModuleName(uint32_t dwId)
{
	print_enter;
	auto it = m_Modules.find(dwId);
	char *ret = it != m_Modules.end() ? const_cast<char*>(it->second.c_str()) : nullptr;
	if (ret != nullptr)
	{
		print_exit_ok;
	}
	else
	{
		print_exit_error;
	}
	return ret;
}

BOOL CDbxMDBX::EnumModuleNames(DBMODULEENUMPROC pFunc, void *pParam)
{
	print_enter;
	for (auto it = m_Modules.begin(); it != m_Modules.end(); ++it)
	{
		if (int ret = pFunc(it->second.c_str(), pParam))
		{
			print_exit_ok;
			return ret;
		}
	}
	print_exit_error;
	return 0;
}
