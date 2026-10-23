Arlia crate holds 500 Fol
=========================

A sample mod for Shattered Stars, made with tools/so2_mod.py:

    so2_mod.py chest DISC 18 0x962 --fol 500 -o crate.patch
    so2_mod.py chest DISC 21 0x962 --fol 500 -o crate.patch

The crate in front of Hearn's general store (chest flag 0x962) appears in two versions of
Arlia's square, map 18 and map 21, so the mod changes both. Opening it gives 500 Fol
instead of 200; it still opens once.
