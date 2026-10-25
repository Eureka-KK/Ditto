#pragma once

// Shared by the UI, schema upgrade and database regression tests.
// Clipboard payloads remain in Data; these statements only update metadata.
namespace ClipboardTabSql
{
inline constexpr auto AddNoteColumn = _T("ALTER TABLE Main ADD COLUMN clipNote TEXT NOT NULL DEFAULT ''");
inline constexpr auto AddOrderColumn = _T("ALTER TABLE Main ADD COLUMN quickPasteTabOrder INTEGER NOT NULL DEFAULT 0");
inline constexpr auto CreateIndex = _T("CREATE INDEX IF NOT EXISTS Main_ClipboardTabs ON Main(quickPasteTabOrder, lID) WHERE bIsGroup = 1 AND quickPasteTabOrder > 0");
inline constexpr auto HistoryFilter = _T("(Main.bIsGroup = 0)");
inline constexpr auto ListTabs = _T("SELECT lID, mText FROM Main WHERE bIsGroup = 1 AND quickPasteTabOrder > 0 ORDER BY quickPasteTabOrder, lID");
inline constexpr auto Pin = _T("UPDATE Main SET quickPasteTabOrder = (SELECT COALESCE(MAX(quickPasteTabOrder), 0) + 1 FROM Main WHERE bIsGroup = 1 AND quickPasteTabOrder > 0) WHERE lID = ? AND bIsGroup = 1 AND quickPasteTabOrder = 0");
inline constexpr auto Assign = _T("UPDATE Main SET lParentID = ?, clipGroupOrder = ?, lDontAutoDelete = CASE WHEN lDontAutoDelete > 0 THEN lDontAutoDelete ELSE ? END WHERE lID = ? AND bIsGroup = 0 AND EXISTS(SELECT 1 FROM Main g WHERE g.lID = ? AND g.bIsGroup = 1 AND g.quickPasteTabOrder > 0)");
inline constexpr auto SaveNote = _T("UPDATE Main SET clipNote = ? WHERE lID = ? AND bIsGroup = 0");
inline constexpr auto SwapRanks = _T("UPDATE Main SET quickPasteTabOrder = CASE lID WHEN ? THEN ? ELSE ? END WHERE lID IN (?, ?)");
}
