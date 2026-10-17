"""Regression for relocated OSGB source aliases; no real GIS files are changed."""
import os
import tempfile
from pathlib import Path
from build_zhumadian_stage_asset import ensure_ascii_source_alias, StageAssetError, repo_root

def main():
    if os.name != 'nt':
        print('SKIP: Windows junction regression')
        return
    work = repo_root()/'work'
    work.mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix='source_alias_test_', dir=work) as directory:
        root = Path(directory)
        old = root/'旧数据'
        source = root/'新数据'
        alias = root/'zmd_source'
        old.mkdir()
        source.mkdir()
        try:
            assert ensure_ascii_source_alias(old, alias) == alias
            assert ensure_ascii_source_alias(old, alias) == alias
            os.rmdir(old)
            assert alias.is_junction() and not alias.exists()
            assert ensure_ascii_source_alias(source, alias) == alias
            assert os.path.samefile(source, alias)
        finally:
            if alias.is_junction():
                os.rmdir(alias)
        # Existing ordinary directories and live aliases to a different target
        # must remain intact and be rejected.
        alias.mkdir()
        try:
            ensure_ascii_source_alias(source, alias)
            raise AssertionError('ordinary directory was replaced')
        except StageAssetError:
            assert alias.is_dir() and not alias.is_junction()
        os.rmdir(alias)
        other = root/'其它数据'
        other.mkdir()
        try:
            ensure_ascii_source_alias(other, alias)
            try:
                ensure_ascii_source_alias(source, alias)
                raise AssertionError('live alias was replaced')
            except StageAssetError:
                assert os.path.samefile(other, alias)
        finally:
            if alias.is_junction():
                os.rmdir(alias)
    print('SOURCE ALIAS MIGRATION PASS: reuse, stale repair, ordinary-dir/live-target protection')

if __name__ == '__main__':
    main()
