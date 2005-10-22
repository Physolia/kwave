# Copyright 1999-2005 Gentoo Foundation
# Distributed under the terms of the GNU General Public License v2
# $Header$

inherit kde flag-o-matic

DESCRIPTION="Kwave is a sound editor for KDE."
HOMEPAGE="http://kwave.sourceforge.net/"
SRC_URI="mirror://sourceforge/kwave/${P}.tar.gz"

SLOT="0"
LICENSE="GPL-2"
KEYWORDS="~x86"
IUSE="mmx"

RDEPEND="kde-base/arts
	media-libs/alsa-lib
	media-libs/audiofile
	media-libs/id3lib
	media-libs/libmad
	media-libs/libogg
	media-libs/libvorbis
	media-libs/flac
	sci-libs/gsl"
DEPEND="${RDEPEND}
	|| ( kde-base/kdesdk-misc kde-base/kdesdk )
	sys-apps/sed
	sys-apps/gawk
	sys-apps/coreutils
	sys-apps/findutils
	app-text/recode
	media-gfx/imagemagick"
need-kde 3.4

src_compile() {
	libtoolize --copy --force
	aclocal-1.9 -I ./admin
	autoheader
	automake-1.9 --foreign --add-missing --copy --include-deps
	autoconf

	local myconf="--without-builtin-libaudiofile"

	use mmx && append-flags "-mmmx"

	kde_src_compile
}
