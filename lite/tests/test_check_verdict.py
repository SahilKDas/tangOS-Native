import unittest
from check_verdict import verdict


class CheckVerdicts(unittest.TestCase):
    def test_cancelled(self):
        self.assertEqual(verdict('ROM data comparison', 1223, '')['state'], 'cancelled')

    def test_actual_partial_rom_report(self):
        report = '  VERIFIED 914 (47,812 bytes)\n  PARTIAL 357\n  DIFFERS 3\n  UNNAMED 59\n'
        value = verdict('ROM data comparison', 0, report)
        self.assertEqual(value['state'], 'completed-with-differences')
        self.assertEqual(value['dataCounts'], dict(verified=914, partial=357, differs=3, unnamed=59))

    def test_empty_and_partial_do_not_pass(self):
        for report, state in [('VERIFIED 0\nPARTIAL 0\nDIFFERS 0\nUNNAMED 0', 'skipped: no data verified'),
                              ('VERIFIED 4\nPARTIAL 1\nDIFFERS 0\nUNNAMED 0', 'completed-with-partial-data'),
                              ('VERIFIED 4', 'unverified: incomplete data report')]:
            self.assertEqual(verdict('ROM data comparison', 0, report)['state'], state)

    def test_clean_report_and_failed_process(self):
        report = 'VERIFIED 1,234\nPARTIAL 0\nDIFFERS 0\nUNNAMED 0'
        self.assertEqual(verdict('ROM data comparison', 0, report)['state'], 'passed')
        self.assertEqual(verdict('ROM data comparison', 1, report)['state'], 'failed')

    def test_other_gates_keep_their_semantics(self):
        self.assertEqual(verdict('Declaration agreement', 0, '12228 disagreement(s)')['state'], 'passed-with-baseline')
        self.assertEqual(verdict('Link checks', 0, 'nothing to verify')['state'], 'skipped')
        self.assertEqual(verdict('Port references', 0, 'all references resolve')['state'], 'passed')


if __name__ == '__main__':
    unittest.main()
