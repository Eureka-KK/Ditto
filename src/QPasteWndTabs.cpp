#include "stdafx.h"
#include "CP_Main.h"
#include "QPasteWnd.h"
#include "GroupName.h"
#include "MoveToGroupDlg.h"
#include "ProcessPaste.h"
#include "ClipboardTabSql.h"
#include <algorithm>

namespace
{
// Keep the popup alive while a menu or owned dialog has the focus.
class ClipboardTabPopupGuard
{
	bool& m_flag;
	bool m_previous;
public:
	explicit ClipboardTabPopupGuard(bool& flag) : m_flag(flag), m_previous(flag) { flag = false; }
	~ClipboardTabPopupGuard() { m_flag = m_previous; }
};

CString TabString(LPCTSTR key, LPCTSTR fallback)
{
	return theApp.m_Language.GetString(key, fallback);
}

CString MenuLabel(CString label)
{
	label.Replace(_T("&"), _T("&&"));
	return label;
}

bool AskTabText(CWnd* owner, CString& text, const CString& title, int limit, bool allowEmpty)
{
	CGroupName dialog(owner);
	dialog.m_title = title;
	dialog.m_csName = text;
	dialog.m_maxLength = limit;
	if (dialog.DoModal() != IDOK)
		return false;
	text = dialog.m_csName;
	text.Trim();
	if (!allowEmpty && text.IsEmpty())
	{
		owner->MessageBox(TabString(_T("ClipboardTabNameRequired"), _T("Enter a tab name.")), _T("Ditto"), MB_OK);
		return false;
	}
	return true;
}
}

void CQPasteWnd::ReloadClipboardTabs()
{
	if (!m_clipboardTabs.GetSafeHwnd())
		return;
	try
	{
		std::vector<ClipboardTab> tabs;
		CppSQLite3Query query = theApp.m_db.execQuery(ClipboardTabSql::ListTabs);
		while (!query.eof())
		{
			tabs.push_back({query.getIntField(_T("lID")), query.getStringField(_T("mText"))});
			query.nextRow();
		}
		bool changed = tabs.size() != m_clipboardTabGroups.size() || m_clipboardTabs.GetItemCount() == 0;
		for (size_t i = 0; !changed && i < tabs.size(); ++i)
			changed = tabs[i].groupId != m_clipboardTabGroups[i].groupId || tabs[i].name != m_clipboardTabGroups[i].name;
		if (changed)
		{
			m_clipboardTabGroups.swap(tabs);
			m_clipboardTabs.DeleteAllItems();
			m_clipboardTabs.InsertItem(0, TabString(_T("ClipboardTabHistory"), _T("History")));
			for (size_t i = 0; i < m_clipboardTabGroups.size(); ++i)
				m_clipboardTabs.InsertItem((int)i + 1, m_clipboardTabGroups[i].name);
		}
		SyncClipboardTabSelection();
	}
	CATCH_SQLITE_EXCEPTION
}

void CQPasteWnd::SyncClipboardTabSelection()
{
	int selected = theApp.m_GroupID < 0 ? 0 : -1;
	for (size_t i = 0; i < m_clipboardTabGroups.size(); ++i)
		if (m_clipboardTabGroups[i].groupId == theApp.m_GroupID)
			selected = (int)i + 1;
	m_clipboardTabs.SetCurSel(selected);
}

void CQPasteWnd::SelectClipboardTab(int index)
{
	if (index < 0 || index > (int)m_clipboardTabGroups.size())
		return;
	int groupId = index == 0 ? -1 : m_clipboardTabGroups[index - 1].groupId;
	KillTimer(2); // TIMER_DO_SEARCH: do not apply the preceding tab's delayed search.
	m_bHandleSearchTextChange = false;
	m_search.SetWindowText(_T(""));
	m_bHandleSearchTextChange = true;
	m_bShowStarredClips = false;
	bool sameGroup = theApp.m_GroupID == groupId;
	if (theApp.EnterGroupID(groupId) && sameGroup)
		FillList();
	SyncClipboardTabSelection();
	if (IsWindowVisible())
		m_lstHeader.SetFocus();
}

void CQPasteWnd::ResetClipboardTabsToHistory()
{
	SelectClipboardTab(0);
}

void CQPasteWnd::OnClipboardTabChanged(NMHDR*, LRESULT* result)
{
	SelectClipboardTab(m_clipboardTabs.GetCurSel());
	*result = 0;
}

bool CQPasteWnd::PinClipboardGroup(int groupId)
{
	try
	{
		// Menu command IDs have a fixed reserved range; never allow an overflowing index.
		int capacity = ID_CLIPBOARD_TAB_DEST_LAST - ID_CLIPBOARD_TAB_DEST_FIRST + 1;
		int count = theApp.m_db.execScalar(_T("SELECT COUNT(*) FROM Main WHERE bIsGroup = 1 AND quickPasteTabOrder > 0"));
		if (count >= capacity)
			return false;
		CppSQLite3Statement update = theApp.m_db.compileStatement(ClipboardTabSql::Pin);
		update.bind(1, groupId);
		update.execDML();
		ReloadClipboardTabs();
		return true;
	}
	CATCH_SQLITE_EXCEPTION_AND_RETURN(false)
}

int CQPasteWnd::CreateClipboardTab()
{
	ClipboardTabPopupGuard guard(m_bHideWnd);
	CString name;
	if (!AskTabText(this, name, TabString(_T("ClipboardTabNew"), _T("New tab")), 64, false))
		return -1;
	try
	{
		CppSQLite3Statement find = theApp.m_db.compileStatement(_T("SELECT lID FROM Main WHERE bIsGroup = 1 AND mText = ? COLLATE NOCASE"));
		find.bind(1, name);
		CppSQLite3Query query = find.execQuery();
		if (!query.eof())
		{
			MessageBox(TabString(_T("ClipboardTabNameExists"), _T("A group with this name already exists. Use Pin existing group from the + menu.")), _T("Ditto"), MB_OK);
			return -1;
		}
	}
	CATCH_SQLITE_EXCEPTION_AND_RETURN(-1)
	int id = NewGroupID(-1, name);
	if (id > 0 && PinClipboardGroup(id))
		return id;
	return -1;
}

void CQPasteWnd::OnNewClipboardTab()
{
	// The + button also exposes existing groups without copying their contents.
	ClipboardTabPopupGuard guard(m_bHideWnd);
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, TabString(_T("ClipboardTabNew"), _T("New tab...")));
	menu.AppendMenu(MF_STRING, 2, TabString(_T("ClipboardTabPin"), _T("Pin existing group...")));
	CRect rect;
	m_addClipboardTab.GetWindowRect(rect);
	UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, rect.left, rect.bottom, this);
	if (command == 1)
		CreateClipboardTab();
	else if (command == 2)
	{
		CMoveToGroupDlg dialog(this, TabString(_T("ClipboardTabPin"), _T("Pin existing group")));
		if (dialog.DoModal() == IDOK && dialog.GetSelectedGroup() > 0)
			PinClipboardGroup(dialog.GetSelectedGroup());
	}
	m_lstHeader.SetFocus();
}

void CQPasteWnd::AppendClipboardTabDestinations(CMenu& menu)
{
	for (size_t i = 0; i < m_clipboardTabGroups.size(); ++i)
		menu.AppendMenu(MF_STRING, ID_CLIPBOARD_TAB_DEST_FIRST + (UINT)i, MenuLabel(m_clipboardTabGroups[i].name));
	if (!m_clipboardTabGroups.empty())
		menu.AppendMenu(MF_SEPARATOR);
	menu.AppendMenu(MF_STRING, ID_CLIPBOARD_TAB_NEW, TabString(_T("ClipboardTabNew"), _T("New tab...")));
}

void CQPasteWnd::AddClipboardTabCommands(CMenu* menu)
{
	ReloadClipboardTabs();
	CMenu destinations;
	destinations.CreatePopupMenu();
	AppendClipboardTabDestinations(destinations);
	bool selected = m_lstHeader.GetSelectedCount() > 0;
	menu->InsertMenu(0, MF_BYPOSITION | MF_POPUP | (selected ? MF_ENABLED : MF_GRAYED),
		(UINT_PTR)destinations.Detach(), TabString(_T("ClipboardTabAdd"), _T("Add to tab")) + (m_actions.ContainsShortcut(ACCEL_MAKEKEY('M', HOTKEYF_CONTROL)) ? _T("") : _T("\tCtrl+M")));
	menu->InsertMenu(1, MF_BYPOSITION | MF_STRING | (m_lstHeader.GetSelectedCount() == 1 ? MF_ENABLED : MF_GRAYED),
		ID_CLIPBOARD_NOTE, TabString(_T("ClipboardNoteEdit"), _T("Edit note...")));
	menu->InsertMenu(2, MF_BYPOSITION | MF_SEPARATOR);
}

void CQPasteWnd::ShowClipboardTabAddMenu()
{
	if (m_lstHeader.GetSelectedCount() == 0)
		return;
	ClipboardTabPopupGuard guard(m_bHideWnd);
	ReloadClipboardTabs();
	CMenu menu;
	menu.CreatePopupMenu();
	AppendClipboardTabDestinations(menu);
	CRect rect;
	int selected = m_lstHeader.GetCaret();
	if (selected < 0 || !m_lstHeader.GetItemRect(selected, rect, LVIR_BOUNDS))
		m_lstHeader.GetClientRect(rect);
	m_lstHeader.ClientToScreen(rect);
	UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, rect.left, rect.bottom, this);
	if (command == ID_CLIPBOARD_TAB_NEW)
	{
		OnCreateClipboardTabAndAdd();
	}
	else if (command >= ID_CLIPBOARD_TAB_DEST_FIRST && command <= ID_CLIPBOARD_TAB_DEST_LAST)
		OnClipboardTabDestination(command);
	m_lstHeader.SetFocus();
}

void CQPasteWnd::OnCreateClipboardTabAndAdd()
{
	int id = CreateClipboardTab();
	for (size_t i = 0; i < m_clipboardTabGroups.size(); ++i)
		if (m_clipboardTabGroups[i].groupId == id)
		{
			OnClipboardTabDestination(ID_CLIPBOARD_TAB_DEST_FIRST + (UINT)i);
			break;
		}
}

void CQPasteWnd::OnAddToClipboardTab()
{
	ShowClipboardTabAddMenu();
}

void CQPasteWnd::OnClipboardTabDestination(UINT command)
{
	size_t index = command - ID_CLIPBOARD_TAB_DEST_FIRST;
	if (index >= m_clipboardTabGroups.size())
		return;
	int target = m_clipboardTabGroups[index].groupId;
	CClipIDs ids;
	m_lstHeader.GetSelectionItemData(ids);
	bool transaction = false;
	try
	{
		theApp.m_db.execDML(_T("BEGIN IMMEDIATE"));
		transaction = true;
		CppSQLite3Statement update = theApp.m_db.compileStatement(ClipboardTabSql::Assign);
		for (INT_PTR i = ids.GetCount(); i > 0; --i)
		{
			update.bind(1, target);
			update.bind(2, CClip::GetNewOrder(target, ids[i - 1]));
			update.bind(3, (int)CTime::GetCurrentTime().GetTime());
			update.bind(4, (int)ids[i - 1]);
			update.bind(5, target);
			update.execDML();
			update.reset();
		}
		theApp.m_db.execDML(_T("COMMIT"));
		transaction = false;
	}
	catch (CppSQLite3Exception&)
	{
		if (transaction)
		{
			try { theApp.m_db.execDML(_T("ROLLBACK")); }
			catch (CppSQLite3Exception&) {}
		}
		MessageBox(TabString(_T("ClipboardTabSaveFailed"), _T("The change could not be saved. Please try again.")), _T("Ditto"), MB_OK | MB_ICONERROR);
		return;
	}
	CString search;
	m_search.GetWindowText(search);
	FillList(search); // Retain the current tab and filter; no duplicate payload is created.
}

void CQPasteWnd::OnEditClipboardNote()
{
	if (m_lstHeader.GetSelectedCount() != 1)
		return;
	CClipIDs ids;
	m_lstHeader.GetSelectionItemData(ids);
	if (ids.GetCount() != 1)
		return;
	ClipboardTabPopupGuard guard(m_bHideWnd);
	try
	{
		CString note;
		{
			CppSQLite3Query query = theApp.m_db.execQueryEx(_T("SELECT clipNote FROM Main WHERE lID = %d AND bIsGroup = 0"), ids[0]);
			if (query.eof())
				return;
			note = query.getStringField(_T("clipNote"), _T(""));
		}
		if (!AskTabText(this, note, TabString(_T("ClipboardNoteEdit"), _T("Edit note (not pasted)")), 512, true))
			return;
		CppSQLite3Statement update = theApp.m_db.compileStatement(ClipboardTabSql::SaveNote);
		update.bind(1, note);
		update.bind(2, (int)ids[0]);
		update.execDML();
		CString search;
		m_search.GetWindowText(search);
		FillList(search);
	}
	CATCH_SQLITE_EXCEPTION
	m_lstHeader.SetFocus();
}

void CQPasteWnd::OnClipboardTabMenu(NMHDR*, LRESULT* result)
{
	*result = 0;
	ClipboardTabPopupGuard guard(m_bHideWnd);
	CPoint point;
	GetCursorPos(&point);
	TCHITTESTINFO hit = {};
	hit.pt = point;
	m_clipboardTabs.ScreenToClient(&hit.pt);
	int index = m_clipboardTabs.HitTest(&hit) - 1;
	if (index < 0 || index >= (int)m_clipboardTabGroups.size())
		return; // History cannot be renamed, reordered or removed.
	int id = m_clipboardTabGroups[index].groupId;
	CMenu menu;
	menu.CreatePopupMenu();
	menu.AppendMenu(MF_STRING, 1, TabString(_T("ClipboardTabRename"), _T("Rename...")));
	menu.AppendMenu(MF_STRING | (index > 0 ? MF_ENABLED : MF_GRAYED), 2, TabString(_T("ClipboardTabLeft"), _T("Move left")));
	menu.AppendMenu(MF_STRING | (index + 1 < (int)m_clipboardTabGroups.size() ? MF_ENABLED : MF_GRAYED), 3, TabString(_T("ClipboardTabRight"), _T("Move right")));
	menu.AppendMenu(MF_STRING, 4, TabString(_T("ClipboardTabUnpin"), _T("Unpin tab (keep group and clips)")));
	UINT command = menu.TrackPopupMenu(TPM_RETURNCMD | TPM_NONOTIFY | TPM_RIGHTBUTTON, point.x, point.y, this);
	try
	{
		if (command == 1)
		{
			CString name = m_clipboardTabGroups[index].name;
			if (!AskTabText(this, name, TabString(_T("ClipboardTabRename"), _T("Rename tab")), 64, false))
				return;
			CppSQLite3Statement duplicate = theApp.m_db.compileStatement(_T("SELECT COUNT(*) FROM Main WHERE bIsGroup = 1 AND lID <> ? AND mText = ? COLLATE NOCASE"));
			duplicate.bind(1, id);
			duplicate.bind(2, name);
			CppSQLite3Query query = duplicate.execQuery();
			if (query.getIntField(0) > 0)
			{
				MessageBox(TabString(_T("ClipboardTabNameExists"), _T("A group with this name already exists.")), _T("Ditto"), MB_OK);
				return;
			}
			query.finalize();
			CppSQLite3Statement update = theApp.m_db.compileStatement(_T("UPDATE Main SET mText = ? WHERE lID = ? AND bIsGroup = 1"));
			update.bind(1, name);
			update.bind(2, id);
			update.execDML();
			if (theApp.m_GroupID == id)
				theApp.m_GroupText = name;
		}
		else if (command == 2 || command == 3)
		{
			int otherIndex = index + (command == 2 ? -1 : 1);
			if (otherIndex < 0 || otherIndex >= (int)m_clipboardTabGroups.size())
				return;
			int other = m_clipboardTabGroups[otherIndex].groupId;
			int rank = theApp.m_db.execScalarEx(_T("SELECT quickPasteTabOrder FROM Main WHERE lID = %d"), id);
			int otherRank = theApp.m_db.execScalarEx(_T("SELECT quickPasteTabOrder FROM Main WHERE lID = %d"), other);
			CppSQLite3Statement swap = theApp.m_db.compileStatement(ClipboardTabSql::SwapRanks);
			swap.bind(1, id);
			swap.bind(2, otherRank);
			swap.bind(3, rank);
			swap.bind(4, id);
			swap.bind(5, other);
			swap.execDML();
		}
		else if (command == 4)
		{
			theApp.m_db.execDMLEx(_T("UPDATE Main SET quickPasteTabOrder = 0 WHERE lID = %d AND bIsGroup = 1"), id);
			if (theApp.m_GroupID == id)
				SelectClipboardTab(0);
		}
		if (command != 0)
		{
			ReloadClipboardTabs();
			MoveControls();
		}
	}
	CATCH_SQLITE_EXCEPTION
	m_lstHeader.SetFocus();
}
