/*
 * Hydrogen
 * Copyright(c) 2008-2026 The hydrogen development team [hydrogen-devel@lists.sourceforge.net]
 * Haiku port: Copyright(c) 2026 idumych (https://github.com/idumych)
 *
 * http://www.hydrogen-music.org
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY, without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program. If not, see https://www.gnu.org/licenses
 *
 */


#include "Autosave.h"

#include <Alert.h>
#include <Catalog.h>
#include <String.h>

#include <QDir>
#include <QFile>
#include <QFileInfo>

#include <core/Helpers/Filesystem.h>

#undef B_TRANSLATION_CONTEXT
#define B_TRANSLATION_CONTEXT "Autosave"

using namespace H2Core;

namespace Autosave
{

QString offerRecovery( const QString& sSongPath )
{
	const QFileInfo song( sSongPath );
	QString sBaseName = song.completeBaseName();
	// A hidden song file has an empty base name otherwise.
	if ( sBaseName.startsWith( "." ) ) {
		sBaseName.remove( 0, 1 );
	}
	const QString sDir = song.absoluteDir().absolutePath();
	// The current hidden name, and the visible one of older versions.
	const QFileInfo recent( QString( "%1/.%2.autosave%3" ).arg( sDir ).arg( sBaseName ).arg( Filesystem::sSongSuffix ) );
	const QFileInfo old( QString( "%1/%2.autosave%3" ).arg( sDir ).arg( sBaseName ).arg( Filesystem::sSongSuffix ) );
	const bool bUntitled = sSongPath == Filesystem::emptyPath( Filesystem::Artifact::Song );

	QString sRecover;
	if ( recent.exists() && ( bUntitled || ! song.exists() || recent.lastModified() > song.lastModified() ) ) {
		sRecover = recent.absoluteFilePath();
	}
	else if ( old.exists() && song.exists() && old.lastModified() > song.lastModified() ) {
		sRecover = old.absoluteFilePath();
	}
	// Not writable next to the song: Filesystem saved it elsewhere.
	if ( sRecover.isEmpty() && ! bUntitled ) {
		const QFileInfo moved( Filesystem::getAutoSavePath( Filesystem::Artifact::Song, sSongPath ) );
		if ( moved.exists() && moved.absoluteFilePath() != recent.absoluteFilePath() &&
			 ( ! song.exists() || moved.lastModified() > song.lastModified() ) ) {
			sRecover = moved.absoluteFilePath();
		}
	}
	if ( sRecover.isEmpty() ) {
		return QString();
	}

	BString sText;
	if ( bUntitled ) {
		sText = B_TRANSLATE( "The new song has changes that were never saved." );
	}
	else {
		sText = B_TRANSLATE( "The song \"%name%\" has changes that were never saved." );
		sText.ReplaceFirst( "%name%", song.fileName().toUtf8().constData() );
	}
	sText << "\n\n" << B_TRANSLATE( "Do you want to recover them?" );
	BAlert* pAlert = new BAlert( B_TRANSLATE( "Recover changes" ), sText, B_TRANSLATE( "Don't recover" ),
								 B_TRANSLATE( "Recover" ), nullptr, B_WIDTH_AS_USUAL, B_IDEA_ALERT );
	pAlert->SetShortcut( 0, B_ESCAPE );
	return pAlert->Go() == 1 ? sRecover : QString();
}

void remove( const QString& sSongPath )
{
	const QString sAutosave = Filesystem::getAutoSavePath( Filesystem::Artifact::Song, sSongPath );
	if ( ! sAutosave.isEmpty() && QFileInfo::exists( sAutosave ) ) {
		QFile::remove( sAutosave );
	}
}

}
