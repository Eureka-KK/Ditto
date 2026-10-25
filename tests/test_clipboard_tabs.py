"""Data-safety regression tests using the production SQL and original Ditto schema.
Run: python -m unittest discover -s tests -v
"""
import json
from pathlib import Path
import re
import sqlite3
import unittest

ROOT = Path(__file__).resolve().parents[1]
SQL = {
    name: json.loads('"' + value + '"')
    for name, value in re.findall(
        r'inline constexpr auto (\w+) = _T\("((?:[^"\\]|\\.)*)"\);',
        (ROOT / 'src/ClipboardTabSql.h').read_text(encoding='utf-8-sig'),
    )
}


def original_schema(table):
    source = (ROOT / 'src/DatabaseUtilities.cpp').read_text(encoding='utf-8-sig')
    source = source[source.index('BOOL CreateDB('):]
    start = source.index(f'db.execDML(_T("CREATE TABLE {table}(')
    statement = source[start:source.index('));', start) + 3]
    return ''.join(json.loads('"' + token + '"') for token in
                   re.findall(r'_T\("((?:[^"\\]|\\.)*)"\)', statement))


def upgrade(connection):
    columns = {row[1] for row in connection.execute('PRAGMA table_info(Main)')}
    if 'clipNote' not in columns:
        connection.execute(SQL['AddNoteColumn'])
    if 'quickPasteTabOrder' not in columns:
        connection.execute(SQL['AddOrderColumn'])
    connection.execute(SQL['CreateIndex'])


class ClipboardTabDataTests(unittest.TestCase):
    def setUp(self):
        self.db = sqlite3.connect(':memory:')
        self.db.execute(original_schema('Main'))
        self.db.execute(original_schema('Data'))
        self.db.execute("INSERT INTO Main(lID, mText, bIsGroup, lParentID, lDontAutoDelete, CRC) VALUES(1, 'example@example.com', 0, -1, 0, 123)")
        self.payload = 'example@example.com\0'.encode('utf-16le')
        self.db.execute("INSERT INTO Data(lParentID, strClipBoardFormat, ooData) VALUES(1, 'CF_UNICODETEXT', ?)", (self.payload,))
        self.db.executemany('INSERT INTO Main(lID, mText, bIsGroup, lParentID) VALUES(?, ?, 1, -1)', [(10, '邮箱'), (11, "Work & O'Brien")])
        upgrade(self.db)
        self.db.commit()

    def tearDown(self):
        self.db.close()

    def assertPayloadUnchanged(self):
        self.assertEqual(self.db.execute('SELECT ooData FROM Data WHERE lParentID=1').fetchone()[0], self.payload)
        self.assertEqual(self.db.execute('SELECT mText, CRC FROM Main WHERE lID=1').fetchone(), ('example@example.com', 123))
        self.assertEqual(self.db.execute('SELECT COUNT(*) FROM Data').fetchone()[0], 1)
        self.assertEqual(self.db.execute('SELECT COUNT(*) FROM Main WHERE bIsGroup=0').fetchone()[0], 1)

    def test_existing_database_upgrade_is_idempotent_and_preserves_data(self):
        upgrade(self.db)
        self.assertEqual(self.db.execute('SELECT clipNote, quickPasteTabOrder FROM Main WHERE lID=1').fetchone(), ('', 0))
        self.assertPayloadUnchanged()
        self.assertEqual(self.db.execute('PRAGMA integrity_check').fetchone()[0], 'ok')

    def test_partial_upgrade_recovers(self):
        db = sqlite3.connect(':memory:')
        try:
            db.execute(original_schema('Main'))
            db.execute(SQL['AddNoteColumn'])
            upgrade(db)
            upgrade(db)
            columns = {r[1] for r in db.execute('PRAGMA table_info(Main)')}
            self.assertTrue({'clipNote', 'quickPasteTabOrder'} <= columns)
        finally:
            db.close()

    def test_assign_preserves_history_without_copying_payload(self):
        self.db.execute(SQL['Pin'], (10,))
        self.db.execute(SQL['Assign'], (10, 1.0, 100, 1, 10))
        self.assertEqual(self.db.execute('SELECT lParentID, lDontAutoDelete FROM Main WHERE lID=1').fetchone(), (10, 100))
        self.assertEqual(self.db.execute('SELECT lID FROM Main WHERE ' + SQL['HistoryFilter']).fetchall(), [(1,)])
        self.assertPayloadUnchanged()

    def test_reassign_changes_only_one_membership_and_keeps_protection(self):
        for group in (10, 11):
            self.db.execute(SQL['Pin'], (group,))
            self.db.execute(SQL['Assign'], (group, 1.0, 100 + group, 1, group))
        self.assertEqual(self.db.execute('SELECT lParentID, lDontAutoDelete FROM Main WHERE lID=1').fetchone(), (11, 110))
        self.assertPayloadUnchanged()

    def test_invalid_target_and_group_rows_cannot_be_assigned(self):
        self.db.execute(SQL['Assign'], (999, 1.0, 100, 1, 999))
        self.db.execute(SQL['Assign'], (10, 1.0, 100, 1, 10))  # Unpinned group.
        self.db.execute(SQL['Pin'], (10,))
        self.db.execute(SQL['Assign'], (10, 1.0, 100, 11, 10))  # A group, not a clip.
        self.assertEqual(self.db.execute('SELECT lParentID FROM Main WHERE lID IN (1,11) ORDER BY lID').fetchall(), [(-1,), (-1,)])
        self.assertPayloadUnchanged()

    def test_note_unicode_quotes_search_clear_and_payload_separation(self):
        note = "投稿邮箱 · O'Brien 100% _test_"
        self.db.execute(SQL['SaveNote'], (note, 1))
        self.assertEqual(self.db.execute('SELECT clipNote FROM Main WHERE lID=1').fetchone()[0], note)
        self.assertEqual(self.db.execute('SELECT lID FROM Main WHERE mText LIKE ? OR clipNote LIKE ?', ('%投稿%', '%投稿%')).fetchall(), [(1,)])
        self.assertEqual(self.db.execute('SELECT lID FROM Main WHERE mText LIKE ? OR clipNote LIKE ?', ('%example@%', '%example@%')).fetchall(), [(1,)])
        self.assertPayloadUnchanged()
        self.db.execute(SQL['SaveNote'], ('', 1))
        self.assertEqual(self.db.execute('SELECT clipNote FROM Main WHERE lID=1').fetchone()[0], '')

    def test_pin_is_idempotent_and_swap_is_stable(self):
        for group in (10, 11, 10):
            self.db.execute(SQL['Pin'], (group,))
        self.assertEqual(self.db.execute(SQL['ListTabs']).fetchall(), [(10, '邮箱'), (11, "Work & O'Brien")])
        self.db.execute(SQL['SwapRanks'], (10, 2, 1, 10, 11))
        self.assertEqual([r[0] for r in self.db.execute(SQL['ListTabs'])], [11, 10])
        self.db.execute(SQL['SwapRanks'], (10, 1, 2, 10, 11))
        self.assertEqual([r[0] for r in self.db.execute(SQL['ListTabs'])], [10, 11])

    def test_unpin_retains_group_note_and_history(self):
        self.db.execute(SQL['Pin'], (10,))
        self.db.execute(SQL['Assign'], (10, 1.0, 100, 1, 10))
        self.db.execute(SQL['SaveNote'], ('投稿', 1))
        self.db.execute('UPDATE Main SET quickPasteTabOrder=0 WHERE lID=10')
        self.assertEqual(self.db.execute(SQL['ListTabs']).fetchall(), [])
        self.assertEqual(self.db.execute('SELECT lParentID, clipNote FROM Main WHERE lID=1').fetchone(), (10, '投稿'))
        self.assertEqual(self.db.execute('SELECT COUNT(*) FROM Main WHERE ' + SQL['HistoryFilter']).fetchone()[0], 1)
        self.assertPayloadUnchanged()

    def test_tab_list_uses_small_partial_index(self):
        plan = ' '.join(str(r) for r in self.db.execute('EXPLAIN QUERY PLAN ' + SQL['ListTabs']))
        self.assertIn('Main_ClipboardTabs', plan)

    def test_failed_batch_can_roll_back_all_memberships(self):
        self.db.execute(SQL['Pin'], (10,))
        self.db.commit()
        self.db.execute('BEGIN IMMEDIATE')
        self.db.execute(SQL['Assign'], (10, 1.0, 100, 1, 10))
        self.db.rollback()
        self.assertEqual(self.db.execute('SELECT lParentID FROM Main WHERE lID=1').fetchone()[0], -1)
        self.assertPayloadUnchanged()


if __name__ == '__main__':
    unittest.main()
