"""Policy, byte-preservation and no-overwrite tests for the local public exporter."""
from pathlib import Path
import hashlib
import importlib.util
import json
import tempfile
import unittest

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('public_export', ROOT / 'scripts/export-public.py')
export = importlib.util.module_from_spec(spec)
spec.loader.exec_module(export)


class PublicExportTests(unittest.TestCase):
    def test_public_materials_and_pinned_metadata(self):
        for name in sorted(export.LICENSE_FILES) + ['README.md', 'docs/build.md', 'hardware/audio-dsp.epro2',
                     'mechanical/front.stp', 'mechanical/front.3mf',
                     'dsp/ADAU1451/ADAU145xSchematic_0/compiler_output.log',
                     'software/tests/fixtures/sigma/registers.json',
                     'software/components/chime_codec_reference/vendor/esp-idf/components/bt/host/bluedroid/external/libldac-dec/CMakeLists.orig']:
            with self.subTest(name=name):
                self.assertTrue(export.is_public(name))

    def test_private_materials_are_not_publishable(self):
        for name in ['.git/config', 'mechanical/front.prt.11', 'mechanical/a.asm.2',
                     'mechanical/a.drw', 'mechanical/nested/private.stp',
                     'docs/PRD.md', 'LICENSES/private-contract.pdf', 'LICENSES/unreviewed.txt',
                     'docs/internal/software-development-history.md',
                     'docs/validation/trace.json', 'docs/third-party/adau145x-adi.pdf',
                     'docs/third-party/speaker.docx', 'data/recording.wav',
                     'hardware/private-other-project.epro2', 'software/build/app.bin',
                     'software/managed_components/vendor.c', 'scripts/__pycache__/a.pyc',
                     'scripts/test.exe', 'software/main/.env', 'software/main/key.pem',
                     'software/main/.GIT/config', '../README.md', '/README.md',
                     'scripts/../../private', r'scripts\private.py', 'C:/private']:
            with self.subTest(name=name):
                self.assertFalse(export.is_public(name))

    def test_snapshot_keeps_bytes_and_reports_licensing(self):
        tmp = ROOT / 'tmp'
        tmp.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=tmp, prefix='public-export-test-') as directory:
            root = Path(directory).resolve()
            self.assertTrue(root.is_relative_to(tmp.resolve()))
            blobs = {'README.md': b'preview\r\n', 'hardware/audio-dsp.epro2': b'PK\0\xff\r\n'}
            status = export.write_snapshot(root, Path('out/preview'), blobs)
            self.assertEqual(status['status'], 'blocked')
            self.assertFalse((root / 'out/preview/.git').exists())
            for name, content in blobs.items():
                self.assertEqual((root / 'out/preview' / name).read_bytes(), content)
                self.assertEqual(status['files'][name], hashlib.sha256(content).hexdigest())
            self.assertEqual(json.loads((root / 'out/preview/PUBLICATION_STATUS.json').read_text()), status)
            with self.assertRaisesRegex(ValueError, 'already exists'):
                export.write_snapshot(root, Path('out/preview'), blobs)
            self.assertEqual((root / 'out/preview/README.md').read_bytes(), blobs['README.md'])
            for target in [root, root / 'out', root / 'elsewhere', root / 'out/../../escape']:
                with self.subTest(target=target), self.assertRaises(ValueError):
                    export.write_snapshot(root, target, blobs)
            self.assertFalse((root / 'escape').exists())
            with self.assertRaisesRegex(ValueError, 'non-public'):
                export.write_snapshot(root, Path('out/invalid'), {'../secret': b'no'})

    def test_license_package_does_not_clear_upstream_permissions(self):
        complete = {name: b'license / notice text' for name in export.LICENSE_FILES}
        self.assertEqual(export.blockers(complete), [])
        # A root LICENSE alone does not cover hardware, models and dependency terms.
        self.assertIn(export.LICENSE_BLOCKER, export.blockers({'LICENSE': b'Apache'}))
        for name in export.LICENSE_FILES:
            with self.subTest(name=name):
                incomplete = dict(complete)
                incomplete[name] = b'  \n'
                self.assertIn(export.LICENSE_BLOCKER, export.blockers(incomplete))
        complete['software/components/chime_codec_reference/vendor/native_bt/adapter.c'] = b'upstream'
        self.assertEqual(export.blockers(complete), [export.CODEC_BLOCKER])
        tmp = ROOT / 'tmp'
        tmp.mkdir(exist_ok=True)
        with tempfile.TemporaryDirectory(dir=tmp, prefix='public-license-test-') as directory:
            status = export.write_snapshot(Path(directory), Path('out/preview'), complete)
            self.assertEqual(status['status'], 'blocked')
            self.assertEqual(status['blockers'], [export.CODEC_BLOCKER])

    def test_current_workspace_pins_and_required_files(self):
        blobs = export.load_snapshot(ROOT, export.inventory(ROOT))
        self.assertIn('hardware/audio-dsp.epro2', blobs)
        self.assertIn('mechanical/front.stp', blobs)
        self.assertNotIn('mechanical/front.prt.11', blobs)
        self.assertEqual({name for name in blobs if name.startswith('docs/')}, export.PUBLIC_DOCS)
        self.assertTrue(export.LICENSE_FILES <= blobs.keys())
        self.assertEqual(export.blockers(blobs), [export.CODEC_BLOCKER])
        missing = dict(blobs)
        del missing['LICENSES/CC-BY-4.0.txt']
        self.assertIn(export.LICENSE_BLOCKER, export.blockers(missing))
        with self.assertRaisesRegex(ValueError, 'Missing public files'):
            export.load_snapshot(ROOT, [name for name in export.inventory(ROOT)
                                        if name != 'LICENSES/CC-BY-4.0.txt'])
        changed = dict(blobs)
        key = 'dsp/esp32-bindings.json'
        changed[key] += b'changed'
        with self.assertRaisesRegex(ValueError, 'Pinned source changed'):
            export.validate_pins(changed)
        del changed[key]
        with self.assertRaisesRegex(ValueError, 'Pinned source omitted'):
            export.validate_pins(changed)


if __name__ == '__main__':
    unittest.main(verbosity=2)
