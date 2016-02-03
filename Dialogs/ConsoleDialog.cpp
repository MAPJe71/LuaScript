#include "ConsoleDialog.h"
#include "Docking.h"
#include "Notepad_plus_msgs.h"
#include "PluginInterface.h"
#include "resource.h"
#include "SciLexer.h"
#include "Scintilla.h"
#include "WcharMbcsConverter.h"

#include <Commctrl.h>

#pragma comment(lib, "comctl32.lib") 

// Do this for now instead of including windowsx.h just for these two
#define GET_X_LPARAM(lp) ((int)(short)LOWORD(lp))
#define GET_Y_LPARAM(lp) ((int)(short)HIWORD(lp))

ConsoleDialog::ConsoleDialog() :
	DockingDlgInterface(IDD_CONSOLE),
	m_data(new tTbData),
	m_sciOutput(NULL),
	m_sciInput(NULL),
	m_console(NULL),
	m_prompt("> "),
	m_hTabIcon(NULL),
	m_currentHistory(0),
	m_runButtonIsRun(true),
	m_hContext(NULL)
{
	m_historyIter = m_history.end();
}

ConsoleDialog::ConsoleDialog(const ConsoleDialog& other) :
	DockingDlgInterface(other),
	m_data(other.m_data ? new tTbData(*other.m_data) : NULL),
	m_sciOutput(other.m_sciOutput),
	m_sciInput(other.m_sciInput),
	m_console(other.m_console),
	m_prompt(other.m_prompt),
	m_hTabIcon(NULL),
	m_history(other.m_history),
	m_historyIter(other.m_historyIter),
	m_changes(other.m_changes),
	m_currentHistory(other.m_currentHistory),
	m_runButtonIsRun(other.m_runButtonIsRun),
	m_hContext(NULL)
{
}

ConsoleDialog::~ConsoleDialog()
{
	if (m_sciOutput)
	{
		::SendMessage(_hParent, NPPM_DESTROYSCINTILLAHANDLE, 0, reinterpret_cast<LPARAM>(m_sciOutput));
		m_sciOutput = NULL;
	}

	if (m_sciInput)
	{
		::SendMessage(_hParent, NPPM_DESTROYSCINTILLAHANDLE, 0, reinterpret_cast<LPARAM>(m_sciInput));
		m_sciInput = NULL;
	}

	if (m_data)
	{
		delete m_data;
		m_data = NULL;
	}

	if (m_hTabIcon)
	{
		::DestroyIcon(m_hTabIcon);
		m_hTabIcon = NULL;
	}

	if (m_hContext)
	{
		::DestroyMenu(m_hContext);
		m_hContext = NULL;
	}

	m_console = NULL;

}

void ConsoleDialog::initDialog(HINSTANCE hInst, NppData& nppData, ConsoleInterface* console)
{
	DockingDlgInterface::init(hInst, nppData._nppHandle);
	
	Window::init(hInst, nppData._nppHandle);
	createOutputWindow(nppData._nppHandle);
	createInputWindow(nppData._nppHandle);

	m_console = console;
	m_hContext = CreatePopupMenu();
	MENUITEMINFO mi;
	mi.cbSize = sizeof(mi);
	mi.fMask = MIIM_ID | MIIM_STRING;
	mi.fType = MFT_STRING;
	mi.fState = MFS_ENABLED;

	mi.wID = 1;
	mi.dwTypeData = _T("Select all");
	InsertMenuItem(m_hContext, 0, TRUE, &mi);

	mi.wID = 2;
	mi.dwTypeData = _T("Copy");
	InsertMenuItem(m_hContext, 1, TRUE, &mi);

	mi.wID = 3;
	mi.dwTypeData = _T("Clear");
	InsertMenuItem(m_hContext, 3, TRUE, &mi);

	mi.wID = 4;
	mi.dwTypeData = _T("To Input");
	InsertMenuItem(m_hContext, 4, TRUE, &mi);
}

BOOL CALLBACK ConsoleDialog::run_dlgProc(UINT message, WPARAM wParam, LPARAM lParam)
{
	switch(message)
	{
		case WM_INITDIALOG:
			{
				SetParent(m_sciOutput, _hSelf);
				ShowWindow(m_sciOutput, SW_SHOW);
				SetParent(m_sciInput, _hSelf);
				ShowWindow(m_sciInput, SW_SHOW);
				HFONT hCourier = CreateFont(14,0,0,0,FW_DONTCARE,FALSE,FALSE,FALSE,DEFAULT_CHARSET,OUT_OUTLINE_PRECIS, CLIP_DEFAULT_PRECIS,CLEARTYPE_QUALITY, FIXED_PITCH, _T("Courier New"));
				if (hCourier != NULL)
				{
					SendMessage(::GetDlgItem(_hSelf, IDC_PROMPT), WM_SETFONT, reinterpret_cast<WPARAM>(hCourier), TRUE); 
				}

				// Subclass some stuff
				SetWindowSubclass(m_sciInput, ConsoleDialog::inputWndProc, 0, reinterpret_cast<DWORD_PTR>(this));
				SetWindowSubclass(m_sciOutput, ConsoleDialog::scintillaWndProc, 0, reinterpret_cast<DWORD_PTR>(this));
				::SetFocus(m_sciInput);
				return FALSE;
			}
		case WM_SIZE:
			MoveWindow(m_sciOutput, 0, 0, LOWORD(lParam), HIWORD(lParam)-30, TRUE);
			MoveWindow(::GetDlgItem(_hSelf, IDC_PROMPT), 0, HIWORD(lParam)-25, 30, 25, TRUE);
			MoveWindow(m_sciInput, 30, HIWORD(lParam) - 30, LOWORD(lParam) - 85, 25, TRUE);
			MoveWindow(::GetDlgItem(_hSelf, IDC_RUN), LOWORD(lParam) - 50, HIWORD(lParam) - 30, 50, 25, TRUE);  
			return FALSE;

		case WM_CONTEXTMENU:
			{
				MENUITEMINFO mi;
				mi.cbSize = sizeof(mi);
				mi.fMask = MIIM_STATE;
				if (0 == (callScintilla(SCI_GETSELECTIONSTART) - callScintilla(SCI_GETSELECTIONEND)))
				{
					mi.fState = MFS_DISABLED;
				}
				else
				{
					mi.fState = MFS_ENABLED;
				}
				
				SetMenuItemInfo(m_hContext, 2, FALSE, &mi);

				// Thanks MS for corrupting the value of BOOL. :-/
				// From the documentation (http://msdn.microsoft.com/en-us/library/ms648002.aspx):
				//
				//    If you specify TPM_RETURNCMD in the uFlags parameter, the return value is the menu-item 
				//    identifier of the item that the user selected. If the user cancels the menu without making 
				//    a selection, or if an error occurs, then the return value is zero.
				INT cmdID = (INT)TrackPopupMenu(m_hContext, TPM_RETURNCMD, GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam), 0, _hSelf, NULL);

				switch(cmdID)
				{
					case 1: // Select All
						callScintilla(SCI_SELECTALL);
						break;

					case 2: // Copy
						callScintilla(SCI_COPY);
						break;

					case 3: // Clear
						clearText();
						break;

					case 4: // To input (TODO: TEST only!)
						giveInputFocus();
						break;

					default:
						break;
				}
			}
			break;
		case WM_COMMAND:
			if (LOWORD(wParam) == IDC_RUN)
			{
				if (m_runButtonIsRun)
				{
					runStatement();
				}
				else
				{
					assert(m_console != NULL);
					if (m_console)
					{
						m_console->stopStatement();
					}
				}
				return FALSE;
			}
			break;

		case WM_SETFOCUS:
			//giveInputFocus();
			OutputDebugString(_T("ConsoleDialog SetFocus\r\n"));
			return FALSE;

		case WM_ACTIVATE:
			if (wParam == WA_ACTIVE)
			{
				OutputDebugString(_T("ConsoleDialog WM_ACTIVATE WA_ACTIVE\r\n"));
				giveInputFocus();
			}
			break;

		case WM_CHILDACTIVATE:
			OutputDebugString(_T("ConsoleDialog WM_CHILDACTIVATE\r\n"));
			giveInputFocus();
			break;

		case WM_NOTIFY:
			{
				LPNMHDR nmhdr = reinterpret_cast<LPNMHDR>(lParam);
				if (m_sciOutput == nmhdr->hwndFrom)
				{
					switch(nmhdr->code)
					{
						case SCN_STYLENEEDED:
							onStyleNeeded(reinterpret_cast<SCNotification*>(lParam));
							return FALSE;

						case SCN_HOTSPOTCLICK:
							onHotspotClick(reinterpret_cast<SCNotification*>(lParam));
							return FALSE;
						
						default:
							break;
					}
				}
				break;
			}
		default:
			break;

	}

	return DockingDlgInterface::run_dlgProc(message, wParam, lParam);

}


void ConsoleDialog::historyPrevious()
{
	if (m_currentHistory > 0)
	{
		const char *text = (const char *)SendMessage(m_sciInput, SCI_GETCHARACTERPOINTER, 0, 0);
		auto wtext = WcharMbcsConverter::char2tchar(text);

		// Not an empty string and different from orig
		if (wtext.get()[0] && (m_historyIter == m_history.end() || *m_historyIter != wtext.get()))
		{
			if (m_changes.find(m_currentHistory) == m_changes.end())
			{
				m_changes.insert(std::pair<int, tstring>(m_currentHistory, tstring(wtext.get())));
			}
			else
			{
				m_changes[m_currentHistory] = tstring(wtext.get());
			}
		}
		//delete[] buffer;

		--m_currentHistory;
		--m_historyIter;

		// If there's no changes to the line, just copy the original
		if (m_changes.find(m_currentHistory) == m_changes.end())
		{
			SendMessage(m_sciInput, SCI_SETTEXT, 0, (LPARAM)WcharMbcsConverter::tchar2char(m_historyIter->c_str()).get());
			// Go to end?
			SendMessage(m_sciInput, SCI_GOTOPOS, SendMessage(m_sciInput, SCI_GETLENGTH, 0, 0), 0);
		}
		else
		{
			// Set it as the changed string
			SendMessage(m_sciInput, SCI_SETTEXT, 0, (LPARAM)WcharMbcsConverter::tchar2char(m_changes[m_currentHistory].c_str()).get());
			// Go to end?
			SendMessage(m_sciInput, SCI_GOTOPOS, SendMessage(m_sciInput, SCI_GETLENGTH, 0, 0), 0);
		}

	}
}

void ConsoleDialog::historyNext()
{
	if (static_cast<size_t>(m_currentHistory) < m_history.size())
	{
		const char *text = (const char *)SendMessage(m_sciInput, SCI_GETCHARACTERPOINTER, 0, 0);
		auto wtext = WcharMbcsConverter::char2tchar(text);

		// Not an empty string and different from orig
		if (wtext.get()[0] && *m_historyIter != wtext.get())
		{
			if (m_changes.find(m_currentHistory) == m_changes.end())
			{
				m_changes.insert(std::pair<int, tstring>(m_currentHistory, tstring(wtext.get())));
			}
			else
			{
				m_changes[m_currentHistory] = tstring(wtext.get());
			}
		}
		//delete[] buffer;

		++m_currentHistory;
		++m_historyIter;

		// If there's no changes to the line, just copy the original
		if (m_changes.find(m_currentHistory) == m_changes.end())
		{
			if (m_historyIter != m_history.end())
			{
				SendMessage(m_sciInput, SCI_SETTEXT, 0, (LPARAM)WcharMbcsConverter::tchar2char(m_historyIter->c_str()).get());
				SendMessage(m_sciInput, SCI_GOTOPOS, SendMessage(m_sciInput, SCI_GETLENGTH, 0, 0), 0);
			}
			else
			{
				SendMessage(m_sciInput, SCI_CLEARALL, 0, 0);
			}
		}
		else
		{
			// Set it as the changed string
			SendMessage(m_sciInput, SCI_SETTEXT, 0, (LPARAM)WcharMbcsConverter::tchar2char(m_changes[m_currentHistory].c_str()).get());
			SendMessage(m_sciInput, SCI_GOTOPOS, SendMessage(m_sciInput, SCI_GETLENGTH, 0, 0), 0);
		}
	}
}


void ConsoleDialog::historyAdd(const TCHAR *line)
{
	if (line && line[0] && !m_history.empty() && line != m_history.back())
	{
		m_history.push_back(tstring(line));
		m_currentHistory = m_history.size();
	}

	m_historyIter = m_history.end();
	m_changes.clear();
}

void ConsoleDialog::historyEnd()
{
	m_currentHistory = m_history.size();
	m_historyIter = m_history.end();
	SendMessage(m_sciInput, SCI_CLEARALL, 0, 0);
}

LRESULT CALLBACK ConsoleDialog::inputWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	ConsoleDialog *cd = reinterpret_cast<ConsoleDialog *>(dwRefData);
	switch (uMsg) {
		case WM_GETDLGCODE:
			return DLGC_WANTARROWS | DLGC_WANTCHARS;
		case WM_KEYDOWN:
			switch (wParam) {
				case VK_UP:
					cd->historyPrevious();
					return FALSE;
				case VK_DOWN:
					cd->historyNext();
					return FALSE;
			}
		case WM_KEYUP:
			switch (wParam) {
				case VK_RETURN:
					cd->runStatement();
					return FALSE;
				case VK_ESCAPE:
					cd->historyEnd();
					return FALSE;
			}
	}
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ConsoleDialog::runStatement()
{
	assert(m_console != NULL);
	if (m_console)
	{
		const char *text = (const char *)SendMessage(m_sciInput, SCI_GETCHARACTERPOINTER, 0, 0);
		writeText(m_prompt.size(), m_prompt.c_str());
		writeText(strlen(text), text);
		writeText(2, "\r\n");
		historyAdd(WcharMbcsConverter::char2tchar(text).get());
		m_console->runStatement(text);
		SendMessage(m_sciInput, SCI_CLEARALL, 0, 0);
	}
}


void ConsoleDialog::stopStatement()
{
	assert(m_console != NULL);
	if (m_console)
	{
		m_console->stopStatement();
	}
}


void ConsoleDialog::setPrompt(const char *prompt)
{
	// NOTE: This doesn't seem to work at all
	m_prompt = prompt;
	::SetWindowTextA(::GetDlgItem(_hSelf, IDC_PROMPT), prompt);
}

void ConsoleDialog::createOutputWindow(HWND hParentWindow)
{
	m_sciOutput = (HWND)::SendMessage(_hParent, NPPM_CREATESCINTILLAHANDLE, 0, reinterpret_cast<LPARAM>(hParentWindow));
	
	LONG currentStyle = GetWindowLong(m_sciOutput, GWL_STYLE);
	SetWindowLong(m_sciOutput, GWL_STYLE, currentStyle | WS_TABSTOP);

	callScintilla(SCI_SETREADONLY, 1, 0);

	/*  Style bits
	 *  LSB  0 - stderr = 1
	 *       1 - hotspot 
	 *       2 - warning
	 *       ... to be continued
	 */

	// Set the codepage to UTF-8
	callScintilla(SCI_SETCODEPAGE, 65001);

	// 0 is stdout, black text
	callScintilla(SCI_STYLESETSIZE, 0 /* = style number */, 8 /* = size in points */);   

	// 1 is stderr, red text
	callScintilla(SCI_STYLESETSIZE, 1 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETFORE, 1, RGB(250, 0, 0));

	// 2 is stdout, black text, underline hotspot
	callScintilla(SCI_STYLESETSIZE, 2 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETUNDERLINE, 2 /* = style number */, 1 /* = underline */);   
	callScintilla(SCI_STYLESETHOTSPOT, 2, 1);

	// 3 is stderr, red text, underline hotspot
	callScintilla(SCI_STYLESETSIZE, 3 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETFORE, 3, RGB(250, 0, 0));
	callScintilla(SCI_STYLESETUNDERLINE, 3 /* = style number */, 1 /* = underline */);   
	callScintilla(SCI_STYLESETHOTSPOT, 3, 1);
	
	// 4 stdout warning without hotspot
	callScintilla(SCI_STYLESETSIZE, 4 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETFORE, 4, RGB(199, 175, 7));  // mucky yellow

	// 5 stderr warning without hotspot
	callScintilla(SCI_STYLESETSIZE, 5 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETFORE, 5, RGB(255, 128, 64));  // orange

	// 6 is hotspot, stdout, warning
	callScintilla(SCI_STYLESETSIZE, 6 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETFORE, 6, RGB(199, 175, 7));  // mucky yellow
	callScintilla(SCI_STYLESETUNDERLINE, 6 /* = style number */, 1 /* = underline */);   
	callScintilla(SCI_STYLESETHOTSPOT, 6, 1);

	// 7 is hotspot, stderr, warning
	callScintilla(SCI_STYLESETSIZE, 7 /* = style number */, 8 /* = size in points */);   
	callScintilla(SCI_STYLESETFORE, 7, RGB(255, 128, 64));  // orange
	callScintilla(SCI_STYLESETUNDERLINE, 7 /* = style number */, 1 /* = underline */);   
	callScintilla(SCI_STYLESETHOTSPOT, 7, 1);

	callScintilla(SCI_USEPOPUP, 0);
	callScintilla(SCI_SETLEXER, SCLEX_CONTAINER);
}

void ConsoleDialog::createInputWindow(HWND hParentWindow) {
	m_sciInput = (HWND)::SendMessage(_hParent, NPPM_CREATESCINTILLAHANDLE, 0, reinterpret_cast<LPARAM>(hParentWindow));
	LONG currentStyle = GetWindowLong(m_sciInput, GWL_STYLE);
	SetWindowLong(m_sciInput, GWL_STYLE, currentStyle | WS_TABSTOP);
	SendMessage(m_sciInput, SCI_SETMARGINWIDTHN, 1, 0);

	// Set it as a Lua lexer and use the style from Notepad++
	SendMessage(m_sciInput, SCI_SETLEXER, SCLEX_LUA, 0);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_COMMENT, 0x008000);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_COMMENTLINE, 0x008000);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_COMMENTDOC, 0x808000);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_LITERALSTRING, 0x4A0095);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_PREPROCESSOR, 0x004080);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_WORD, 0xFF0000);
	SendMessage(m_sciInput, SCI_STYLESETBOLD, SCE_LUA_WORD, 1); // keywordClass="instre1"
	SendMessage(m_sciInput, SCI_SETKEYWORDS, 0, (LPARAM)"and break do else elseif end false for function goto if in local nil not or repeat return then true until while");
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_NUMBER, 0x0080FF);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_STRING, 0x808080);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_CHARACTER, 0x808080);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_OPERATOR, 0x800000);
	SendMessage(m_sciInput, SCI_STYLESETBOLD, SCE_LUA_OPERATOR, 1);
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_WORD2, 0xC08000);
	SendMessage(m_sciInput, SCI_STYLESETBOLD, SCE_LUA_WORD2, 1); // keywordClass="instre2"
	SendMessage(m_sciInput, SCI_SETKEYWORDS, 1, (LPARAM)"_ENV _G _VERSION assert collectgarbage dofile error getfenv getmetatable ipairs load loadfile loadstring module next pairs pcall print rawequal rawget rawlen rawset require select setfenv setmetatable tonumber tostring type unpack xpcall string table math bit32 coroutine io os debug package __index __newindex __call __add __sub __mul __div __mod __pow __unm __concat __len __eq __lt __le __gc __mode");
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_WORD3, 0xFF0080);
	SendMessage(m_sciInput, SCI_STYLESETBOLD, SCE_LUA_WORD3, 1); // keywordClass="type1"
	SendMessage(m_sciInput, SCI_SETKEYWORDS, 2, (LPARAM)"byte char dump find format gmatch gsub len lower rep reverse sub upper abs acos asin atan atan2 ceil cos cosh deg exp floor fmod frexp ldexp log log10 max min modf pow rad random randomseed sin sinh sqrt tan tanh arshift band bnot bor btest bxor extract lrotate lshift replace rrotate rshift shift string.byte string.char string.dump string.find string.format string.gmatch string.gsub string.len string.lower string.match string.rep string.reverse string.sub string.upper table.concat table.insert table.maxn table.pack table.remove table.sort table.unpack math.abs math.acos math.asin math.atan math.atan2 math.ceil math.cos math.cosh math.deg math.exp math.floor math.fmod math.frexp math.huge math.ldexp math.log math.log10 math.max math.min math.modf math.pi math.pow math.rad math.random math.randomseed math.sin math.sinh math.sqrt math.tan math.tanh bit32.arshift bit32.band bit32.bnot bit32.bor bit32.btest bit32.bxor bit32.extract bit32.lrotate bit32.lshift bit32.replace bit32.rrotate bit32.rshift");
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_WORD4, 0xA00000);
	SendMessage(m_sciInput, SCI_STYLESETBOLD, SCE_LUA_WORD4, 1);
	SendMessage(m_sciInput, SCI_STYLESETITALIC, SCE_LUA_WORD4, 1); // keywordClass="type2"
	SendMessage(m_sciInput, SCI_SETKEYWORDS, 3, (LPARAM)"close flush lines read seek setvbuf write clock date difftime execute exit getenv remove rename setlocale time tmpname coroutine.create coroutine.resume coroutine.running coroutine.status coroutine.wrap coroutine.yield io.close io.flush io.input io.lines io.open io.output io.popen io.read io.tmpfile io.type io.write io.stderr io.stdin io.stdout os.clock os.date os.difftime os.execute os.exit os.getenv os.remove os.rename os.setlocale os.time os.tmpname debug.debug debug.getfenv debug.gethook debug.getinfo debug.getlocal debug.getmetatable debug.getregistry debug.getupvalue debug.getuservalue debug.setfenv debug.sethook debug.setlocal debug.setmetatable debug.setupvalue debug.setuservalue debug.traceback debug.upvalueid debug.upvaluejoin package.cpath package.loaded package.loaders package.loadlib package.path package.preload package.seeall");
	SendMessage(m_sciInput, SCI_STYLESETFORE, SCE_LUA_LABEL, 0x008080);
	SendMessage(m_sciInput, SCI_STYLESETBOLD, SCE_LUA_LABEL, 1);
}

LRESULT CALLBACK ConsoleDialog::scintillaWndProc(HWND hWnd, UINT uMsg, WPARAM wParam, LPARAM lParam, UINT_PTR uIdSubclass, DWORD_PTR dwRefData)
{
	// No idea what this does, but it seems to help a bit.
	if (uMsg == WM_GETDLGCODE) return DLGC_WANTARROWS | DLGC_WANTCHARS;
	return DefSubclassProc(hWnd, uMsg, wParam, lParam);
}

void ConsoleDialog::writeText(size_t length, const char *text)
{
	::SendMessage(m_sciOutput, SCI_SETREADONLY, 0, 0);
	for (idx_t i = 0; i < length; ++i)
	{
		if (text[i] == '\r')
		{
			::SendMessage(m_sciOutput, SCI_APPENDTEXT, i, reinterpret_cast<LPARAM>(text));
			text += i + 1;
			length -= i + 1;
			i = 0;
		}
	}
	
	if (length > 0)
	{
		::SendMessage(m_sciOutput, SCI_APPENDTEXT, length, reinterpret_cast<LPARAM>(text));
	}

	::SendMessage(m_sciOutput, SCI_SETREADONLY, 1, 0);
	
	::SendMessage(m_sciOutput, SCI_GOTOPOS, ::SendMessage(m_sciOutput, SCI_GETLENGTH, 0, 0), 0);
	
}


void ConsoleDialog::writeError(size_t length, const char *text)
{
	size_t docLength = (size_t)callScintilla(SCI_GETLENGTH);
	size_t realLength = length;
	callScintilla(SCI_SETREADONLY, 0);
	for (idx_t i = 0; i < length; ++i)
	{
		if (text[i] == '\r')
		{
			if (i)
			{
				callScintilla(SCI_APPENDTEXT, i, reinterpret_cast<LPARAM>(text));
			}
			text += i + 1;
			length -= i + 1;
			realLength--;
			i = 0;
		}
	}
	
	if (length > 0)
	{
		callScintilla(SCI_APPENDTEXT, length, reinterpret_cast<LPARAM>(text));
	}

	callScintilla(SCI_SETREADONLY, 1);
	callScintilla(SCI_STARTSTYLING, docLength, 0x01);
	callScintilla(SCI_SETSTYLING, realLength, 1);

	callScintilla(SCI_COLOURISE, docLength, -1);
	callScintilla(SCI_GOTOPOS, docLength + realLength);
}


void ConsoleDialog::doDialog()
{
	if (!isCreated())
	{
		create(m_data);

		assert(m_data);
		if (m_data)
		{
			// define the default docking behaviour
			m_data->uMask = DWS_DF_CONT_BOTTOM | DWS_ICONTAB;
			//m_data->pszName = _T("Lua");
		
			//m_hTabIcon = (HICON)::LoadImage(_hInst, MAKEINTRESOURCE(IDI_PYTHON8), IMAGE_ICON, 16, 16, LR_LOADMAP3DCOLORS | LR_LOADTRANSPARENT);
			//m_data->hIconTab			= m_hTabIcon;
			m_data->pszModuleName	= _T("LuaScript.dll");
			m_data->dlgID			= -1; /* IDD_CONSOLE */
			m_data->pszAddInfo		= NULL; //_pExProp->szCurrentPath;
			m_data->iPrevCont		= -1;
			m_data->hClient			= _hSelf;


			::SendMessage(_hParent, NPPM_DMMREGASDCKDLG, 0, reinterpret_cast<LPARAM>(m_data));

			// Parse the whole doc, in case we've had errors that haven't been parsed yet
			callScintilla(SCI_COLOURISE, 0, -1);
		}
	}

	display(true);
}

void ConsoleDialog::hide()
{
	display(false);
}

void ConsoleDialog::runEnabled(bool enabled)
{
	//EnableWindow(GetDlgItem(_hSelf, IDC_RUN), enabled);
	::SetWindowText(GetDlgItem(_hSelf, IDC_RUN), enabled ? _T("Run") : _T("Stop"));
	m_runButtonIsRun = enabled;

	if (enabled)
	{
		::SetForegroundWindow(_hSelf);
		//::SetActiveWindow(_hSelf);
		::SetFocus(m_sciInput);
	}
}

void ConsoleDialog::clearText()
{
	::SendMessage(m_sciOutput, SCI_SETREADONLY, 0, 0);
	::SendMessage(m_sciOutput, SCI_CLEARALL, 0, 0);
	::SendMessage(m_sciOutput, SCI_SETREADONLY, 1, 0);
}

void ConsoleDialog::onStyleNeeded(SCNotification* notification)
{
	idx_t startPos = (idx_t)callScintilla(SCI_GETENDSTYLED);
	idx_t startLine = (idx_t)callScintilla(SCI_LINEFROMPOSITION, startPos);
	idx_t endPos = (idx_t)notification->position;
	idx_t endLine = (idx_t)callScintilla(SCI_LINEFROMPOSITION, endPos);

	LineDetails lineDetails;
	for(idx_t lineNumber = startLine; lineNumber <= endLine; ++lineNumber)
	{
		lineDetails.lineLength = (size_t)callScintilla(SCI_GETLINE, lineNumber);

		if (lineDetails.lineLength > 0)
		{
			lineDetails.line = new char[lineDetails.lineLength + 1];
			callScintilla(SCI_GETLINE, lineNumber, reinterpret_cast<LPARAM>(lineDetails.line));
			lineDetails.line[lineDetails.lineLength] = '\0';
			lineDetails.errorLevel = EL_UNSET;
			
			
			if (parseLine(&lineDetails))
			{
				startPos = (idx_t)callScintilla(SCI_POSITIONFROMLINE, lineNumber);

				// Check that it's not just a file called '<console>'
				if (strncmp(lineDetails.line + lineDetails.filenameStart, "<console>", lineDetails.filenameEnd - lineDetails.filenameStart))
				{
					int mask, style;
					switch(lineDetails.errorLevel)
					{
						case EL_WARNING:
							mask = 0x04;
							style = 0x04;
							break;

						case EL_ERROR:
							mask = 0x01;
							style = 0x01;
							break;

						case EL_UNSET:
						case EL_INFO:
						default:
							mask = 0x00;
							style = 0x00;
							break;
					}

					if (lineDetails.filenameStart > 0)
					{
						callScintilla(SCI_STARTSTYLING, startPos, mask);
						callScintilla(SCI_SETSTYLING, lineDetails.filenameStart, style);
					}


					callScintilla(SCI_STARTSTYLING, startPos + lineDetails.filenameStart, mask | 0x02);
					callScintilla(SCI_SETSTYLING, lineDetails.filenameEnd - lineDetails.filenameStart, style | 0x02);
					
					if (lineDetails.lineLength > lineDetails.filenameEnd)
					{
						callScintilla(SCI_STARTSTYLING, startPos + lineDetails.filenameEnd, mask);
						callScintilla(SCI_SETSTYLING, lineDetails.lineLength - lineDetails.filenameEnd, style);
					}


				}
			}

			delete[] lineDetails.line;
			
		}
	}

	// ensure that everything is set as styled (just move the endStyled variable on to the requested position)
	callScintilla(SCI_STARTSTYLING, static_cast<WPARAM>(notification->position), 0x0);
}

bool ConsoleDialog::parseLine(LineDetails *lineDetails)
{
	return false;
}

void ConsoleDialog::onHotspotClick(SCNotification* notification)
{
	assert(m_console != NULL);
	if (m_console)
	{
	idx_t lineNumber = callScintilla(SCI_LINEFROMPOSITION, static_cast<WPARAM>(notification->position));
	LineDetails lineDetails;
		lineDetails.lineLength = (size_t)callScintilla(SCI_GETLINE, lineNumber);

		if (lineDetails.lineLength != SIZE_MAX)
		{
			lineDetails.line = new char[lineDetails.lineLength + 1];
			callScintilla(SCI_GETLINE, lineNumber, reinterpret_cast<LPARAM>(lineDetails.line));
			lineDetails.line[lineDetails.lineLength] = '\0';
			if (parseLine(&lineDetails))
			{
				lineDetails.line[lineDetails.filenameEnd] = '\0';
				m_console->openFile(lineDetails.line + lineDetails.filenameStart, lineDetails.errorLineNo);
			}
		}
	}
}
