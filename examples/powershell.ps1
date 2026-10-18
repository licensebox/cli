if (-not $env:LICENSEBOX_PUBLIC_KEYS) {
    throw 'Set LICENSEBOX_PUBLIC_KEYS to the public keys JSON path'
}
$licenseboxArgs = @('--product', 'your-product', '--keys', $env:LICENSEBOX_PUBLIC_KEYS, '--version', '1.0')
& licensebox @licenseboxArgs require export
$authorizationExit = $LASTEXITCODE
if ($authorizationExit -ne 0) { exit $authorizationExit }
# Check access before opening or processing user files.
& your-application export @args
exit $LASTEXITCODE
