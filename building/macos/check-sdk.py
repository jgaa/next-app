#!/usr/bin/env python3
"""Check a `tar -tf` SDK listing before scheduling expensive macOS builds."""
import argparse
import sys

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--version', required=True)
args = parser.parse_args()
entries = {line.strip().removeprefix('./').rstrip('/') for line in sys.stdin}
# Accept the installer directory alone or its versioned parent directory.
prefix = next((candidate for candidate in ('macos', f'{args.version}/macos')
               if f'{candidate}/bin/qt-cmake' in entries), f'{args.version}/macos')
# Application requirements plus QCoro's private Qt development interfaces.
components = (
    'Core Gui Network Quick QuickControls2 Qml Svg Test Concurrent Sql '
    'Protobuf ProtobufQuick Grpc ProtobufQtCoreTypes ProtobufWellKnownTypes '
    'HttpServer QuickPrivate QmlPrivate'
).split()
required = [f'lib/cmake/Qt6{module}/Qt6{module}Config.cmake' for module in components]
required += ['bin/qt-cmake', 'bin/macdeployqt', 'libexec/moc',
             'libexec/rcc', 'libexec/qmlimportscanner',
             'libexec/qtprotobufgen', 'libexec/qtgrpcgen',
             'plugins/platforms/libqcocoa.dylib',
             'plugins/sqldrivers/libqsqlite.dylib',
             'plugins/imageformats/libqsvg.dylib']
missing = [path for path in required if f'{prefix}/{path}' not in entries]
if missing:
    print('::error::Incomplete macOS Qt SDK. Missing:\n' + '\n'.join(missing), file=sys.stderr)
    print('Rebuild/repackage the SDK with these components and bump QT_SDK_REVISION.', file=sys.stderr)
    sys.exit(1)
print(f'Qt {args.version} SDK content check passed ({len(required)} required files).')
