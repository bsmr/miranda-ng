/*

Miranda NG: the free IM client for Microsoft* Windows*

Copyright (�) 2012-17 Miranda NG project,
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
#include "chat.h"

MIR_APP_DLL(DWORD) CALLBACK Srmm_LogStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG * pcb)
{
	LOGSTREAMDATA *lstrdat = (LOGSTREAMDATA*)dwCookie;
	if (lstrdat) {
		// create the RTF
		if (lstrdat->buffer == nullptr) {
			lstrdat->bufferOffset = 0;
			lstrdat->buffer = chatApi.Log_CreateRTF(lstrdat);
			lstrdat->bufferLen = (int)mir_strlen(lstrdat->buffer);
		}

		// give the RTF to the RE control
		*pcb = min(cb, LONG(lstrdat->bufferLen - lstrdat->bufferOffset));
		memcpy(pbBuff, lstrdat->buffer + lstrdat->bufferOffset, *pcb);
		lstrdat->bufferOffset += *pcb;

		// free stuff if the streaming operation is complete
		if (lstrdat->bufferOffset == lstrdat->bufferLen) {
			mir_free(lstrdat->buffer);
			lstrdat->buffer = nullptr;
		}
	}

	return 0;
}

MIR_APP_DLL(DWORD) CALLBACK Srmm_MessageStreamCallback(DWORD_PTR dwCookie, LPBYTE pbBuff, LONG cb, LONG *pcb)
{
	static DWORD dwRead;
	char **ppText = (char **)dwCookie;

	if (*ppText == nullptr) {
		*ppText = (char *)mir_alloc(cb + 2);
		memcpy(*ppText, pbBuff, cb);
		*pcb = cb;
		dwRead = cb;
		*(*ppText + cb) = '\0';
	}
	else {
		char *p = (char *)mir_realloc(*ppText, dwRead + cb + 2);
		memcpy(p + dwRead, pbBuff, cb);
		*ppText = p;
		*pcb = cb;
		dwRead += cb;
		*(*ppText + dwRead) = '\0';
	}
	return 0;
}

EXTERN_C MIR_APP_DLL(LRESULT) CALLBACK Srmm_ButtonSubclassProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
	switch (msg) {
	case WM_RBUTTONUP:
		if (db_get_b(0, CHAT_MODULE, "RightClickFilter", 0) != 0) {
			CSrmmBaseDialog *pDlg = (CSrmmBaseDialog*)GetWindowLongPtr(GetParent(hwnd), GWLP_USERDATA);
			if (pDlg == nullptr)
				break;

			if (hwnd == pDlg->m_pFilter->GetHwnd())
				pDlg->ShowFilterMenu();
			else if (hwnd == pDlg->m_pColor->GetHwnd())
				pDlg->ShowColorChooser(pDlg->m_pColor->GetCtrlId());
			else if (hwnd == pDlg->m_pBkColor->GetHwnd())
				pDlg->ShowColorChooser(pDlg->m_pBkColor->GetCtrlId());
		}
		break;
	}

	return mir_callNextSubclass(hwnd, Srmm_ButtonSubclassProc, msg, wParam, lParam);
}