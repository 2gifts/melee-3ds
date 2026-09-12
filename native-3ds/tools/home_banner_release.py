"""Keep the confirmed banner as the default; explicitly validate cosmetic trials.

Format checks and model-only emulator tests did not catch the selection freezes
in packages 6 and 7. This lock covers the packed CBMD, including compression,
padding, resource offsets and audio. It contains hashes, not game assets.
An explicit cosmetic baseline permits only a verified in-place patch and marks
it as not yet console-tested. It never promotes a candidate to the default.
"""
import hashlib
from verify_home_banner import verify_banner

APPROVED_CGFX = 'c34e3b54bfe4e7659d8647e36b8b7f856833d5b1b0662733e890ab43d666be1e'
APPROVED_CBMD = 'd8f960ac9e747fd018fb550082abeea5695aa5db4cba0975b0d3db28ac8479f5'


def verify_release_model(data, *, cosmetic_baseline=None):
    if cosmetic_baseline is not None:
        from patch_home_banner import verify_cosmetic_model
        return verify_cosmetic_model(data,cosmetic_baseline)
    assert hashlib.sha256(data).hexdigest()==APPROVED_CGFX, (
        'Unconfirmed HOME banner: release packaging is locked to console-tested package 5. '
        'Model-only emulator checks do not authorize a replacement.')
    # Its legacy face orientation is a cosmetic defect. Keep structural
    # validation, but do not modify the exact model confirmed on the console.
    result = verify_banner(data,require_outward=False)
    result.update(release_baseline='console-tested package 5',legacy_appearance=True,console_tested=True)
    return result


def verify_release_container(data, *, cosmetic_baseline=None):
    if cosmetic_baseline is not None:
        from patch_home_banner import verify_cosmetic_container
        return verify_cosmetic_container(data,cosmetic_baseline)
    assert hashlib.sha256(data).hexdigest()==APPROVED_CBMD, (
        'Complete HOME banner differs from the console-tested CBMD, including sound/padding')
