//
// C++ Implementation: track
//
// Description: 
//
//
// Author: Tue Haste Andersen <haste@diku.dk>, (C) 2003
//
// Copyright: See COPYING file that comes with this distribution
//
//

#include "track.h"
#include "trackinfoobject.h"
#include "trackcollection.h"
#include "xmlparse.h"
#include <qfile.h>
#include "mixxxview.h"
#include <qdragobject.h>
#include "wtracktable.h"
#include "wtreeview.h"
#include "wnumberpos.h"
#include <qpopupmenu.h>
#include <qcursor.h>
#include <qcstring.h>
#include "enginebuffer.h"
#include "reader.h"

Track::Track(QString location, MixxxView *pView, EngineBuffer *pBuffer1, EngineBuffer *pBuffer2)
{
    m_pView = pView;
    m_pBuffer1 = pBuffer1;
    m_pBuffer2 = pBuffer2;
    m_pActivePlaylist = 0;
    m_pActivePopupPlaylist = 0;
    m_pTrackPlayer1 = 0;
    m_pTrackPlayer2 = 0;

    m_pTrackCollection = new TrackCollection();

    // Read the XML file
    readXML(location);

    // Ensure that one playlist is present
    if (m_qPlaylists.count()==0)
        m_qPlaylists.append(new TrackPlaylist(m_pTrackCollection));

    // Update tree view
    updateTreeView();

    // Insert the first playlist in the list
    m_pActivePlaylist = m_qPlaylists.at(0);
    m_pActivePlaylist->activate(m_pView->m_pTrackTable);

    
    connect(m_pView->m_pTreeView, SIGNAL(playlistPopup(QString)), this, SLOT(slotPlaylistPopup(QString)));

    // Connect drop events to table
    connect(m_pView->m_pTrackTable, SIGNAL(dropped(QDropEvent *)), this, SLOT(slotDrop(QDropEvent *)));

    // Connect mouse events from WTrackTable
    connect(m_pView->m_pTrackTable, SIGNAL(mousePressed(TrackInfoObject*, int )), this, SLOT(slotTrackPopup(TrackInfoObject*, int )));
}

Track::~Track()
{
}

void Track::readXML(QString location)
{
    // Open XML file
    QFile file(location);
    QDomDocument domXML("Mixxx_Track_List");

    // Check if we can open the file
    if (!file.exists())
    {
        qDebug("Track: %s does not exists.",location.latin1());
        file.close();
        return;
    }

    // Check if there is a parsing problem
    if (!domXML.setContent(&file))
    {
        qDebug("Track: Parse error in %s",location.latin1());
        file.close();
        return;
    }

    file.close();

    // Get the root element
    QDomElement elementRoot = domXML.documentElement();

    // Get version
    int version = XmlParse::selectNodeInt(elementRoot, "Version");

    // Initialize track collection
    QDomNode node = XmlParse::selectNode(elementRoot, "TrackList");
    m_pTrackCollection->readXML(node);

    // Get all the Playlists written in the xml file:
    node = XmlParse::selectNode(elementRoot, "Playlists").firstChild();
    while (!node.isNull())
    {
        if (node.isElement() && node.nodeName()=="Playlist")
            m_qPlaylists.append(new TrackPlaylist(m_pTrackCollection, node));

        node = node.nextSibling();
    }
}

void Track::writeXML(QString location)
{

/*
    // First transfer information from the comment field from the table to the Track:
    for (int iRow=0; iRow<m_pTableTracks->numRows(); iRow++)
    {
        if (m_pTableTracks->item(iRow, COL_INDEX))
        {
            m_lTracks.at( m_pTableTracks->item(iRow, COL_INDEX)->text().toUInt() )->setComment(m_pTableTracks->item(iRow, COL_COMMENT)->text());
        }
    }
*/

    // Create the xml document:
    QDomDocument domXML( "Mixxx_Track_List" );

    // Ensure UTF16 encoding
    domXML.appendChild(domXML.createProcessingInstruction("xml","version=\" 1.0 \" encoding=\"UTF-16\""));

    // Set the document type
    QDomElement elementRoot = domXML.createElement( "Mixxx_Track_List" );
    domXML.appendChild(elementRoot);

    // Add version information:
    XmlParse::addElement(domXML, elementRoot, "Version", QString("%1").arg(TRACK_VERSION));

    // Write collection of tracks
    m_pTrackCollection->writeXML(domXML, elementRoot);

    // Write playlists
    QDomElement playlistsroot = domXML.createElement("Playlists");

    QPtrList<TrackPlaylist>::iterator it = m_qPlaylists.begin();
    while (it!=m_qPlaylists.end())
    {
        QDomElement elementNew = domXML.createElement("Playlist");
        (*it)->writeXML(domXML, elementNew);
        playlistsroot.appendChild(elementNew);

        ++it;
    }
    elementRoot.appendChild(playlistsroot);

    // Open the file:
    QFile opmlFile(location);
    if (!opmlFile.open(IO_WriteOnly))
    {
        QMessageBox::critical(0,
                tr("Error"),
                tr("Cannot open file %1").arg(location));
        return;
    }

    // Write to the file:
    QTextStream Xml(&opmlFile);
    Xml.setEncoding(QTextStream::Unicode);
    Xml << domXML.toString();
    opmlFile.close();
}

void Track::slotDrop(QDropEvent *e)
{
    qDebug("track drop");

    QString name;
    QCString type("playlist");
    if (!QTextDrag::decode(e, name, type))
    {
        e->ignore();
        return;
    }

    e->accept();

    qDebug("name %s",name.latin1());

    // Get pointer to requested playlist
    TrackPlaylist *pNewlist = getPlaylist(name);

    if (pNewlist)
    {
        // Deactivate current playlist
        if (m_pActivePlaylist)
            m_pActivePlaylist->deactivate();

        // Activate new playlist
        m_pActivePlaylist = pNewlist;
        m_pActivePlaylist->activate(m_pView->m_pTrackTable);
    }
}

void Track::slotNewPlaylist()
{
    // Find valid name for new playlist
    int i = 1;
    while (getPlaylist(QString("Default %1").arg(i)))
        ++i;

    m_qPlaylists.append(new TrackPlaylist(m_pTrackCollection, QString("Default %1").arg(i)));
    updateTreeView();
}

void Track::slotDeletePlaylist(QString qName)
{
    TrackPlaylist *list = getPlaylist(qName);
    if (list)
    {
        if (list==m_pActivePlaylist)
            list->deactivate();

        m_qPlaylists.remove(list);
        delete list;
    }
    updateTreeView();
}

void Track::slotDeletePlaylist()
{
    slotDeletePlaylist(m_pActivePopupPlaylist->getListName());
}

TrackPlaylist *Track::getPlaylist(QString qName)
{
    QPtrList<TrackPlaylist>::iterator it = m_qPlaylists.begin();
    while (it!=m_qPlaylists.end())
    {
        if ((*it)->getListName()==qName)
            return (*it);
        ++it;
    }
    return 0;
}

void Track::updateTreeView()
{
    if (m_pView->m_pTreeView)
    {
        QStrList list;
        QPtrList<TrackPlaylist>::iterator it = m_qPlaylists.begin();
        while (it!=m_qPlaylists.end())
        {
            list.append((*it)->getListName());
            ++it;
        }
        m_pView->m_pTreeView->updatePlaylists(list);
    }
}

void Track::slotPlaylistPopup(QString qName)
{
    QPopupMenu *menu = new QPopupMenu();

    m_pActivePopupPlaylist = getPlaylist(qName);

    // If this entry is actually a playlist, make it possible to delete it
    if (m_pActivePopupPlaylist)
        menu->insertItem("Delete", this, SLOT(slotDeletePlaylist()));
    else
        menu->insertItem("New", this, SLOT(slotNewPlaylist()));
    menu->exec(QCursor::pos());
}

void Track::slotTrackPopup(TrackInfoObject *pTrackInfoObject, int)
{
    QPopupMenu *menu = new QPopupMenu();

    m_pActivePopupTrack = pTrackInfoObject;

    menu->insertItem("Player 1", this, SLOT(slotLoadPlayer1()));
    menu->insertItem("Player 2", this, SLOT(slotLoadPlayer2()));

    menu->exec(QCursor::pos());

}

void Track::slotLoadPlayer1(TrackInfoObject *pTrackInfoObject)
{
    m_pTrackPlayer1 = pTrackInfoObject;
    emit(newTrackPlayer1(m_pTrackPlayer1));

/*
    // Update score:
    m_pTrackPlayer1->incTimesPlayed();
    if (m_pTrackPlayer1->getTimesPlayed() > m_iMaxTimesPlayed)
        m_iMaxTimesPlayed = m_pTrackPlayer1->getTimesPlayed();
    UpdateScores();
*/

    // Request a new track from the reader:
    m_pBuffer1->getReader()->requestNewTrack(m_pTrackPlayer1);

    // Set duration in playpos widget
    if (m_pView->m_pNumberPosCh1)
        m_pView->m_pNumberPosCh1->setDuration(m_pTrackPlayer1->getDuration());

    // Write info to text display
    if (m_pView->m_pTextCh1)
        m_pView->m_pTextCh1->setText(m_pTrackPlayer1->getInfo());
}

void Track::slotLoadPlayer2(TrackInfoObject *pTrackInfoObject)
{
    m_pTrackPlayer2 = pTrackInfoObject;
    emit(newTrackPlayer2(m_pTrackPlayer2));

/*
    // Update score:
    m_pTrackPlayer2->incTimesPlayed();
    if (m_pTrackPlayer2->getTimesPlayed() > m_iMaxTimesPlayed)
        m_iMaxTimesPlayed = m_pTrackPlayer2->getTimesPlayed();
    UpdateScores();
*/

    // Request a new track from the reader:
    m_pBuffer2->getReader()->requestNewTrack(m_pTrackPlayer2);

    // Set duration in playpos widget
    if (m_pView->m_pNumberPosCh2)
        m_pView->m_pNumberPosCh2->setDuration(m_pTrackPlayer2->getDuration());

    // Write info to text display
    if (m_pView->m_pTextCh2)
        m_pView->m_pTextCh2->setText(m_pTrackPlayer2->getInfo());
}

void Track::slotLoadPlayer1()
{
    slotLoadPlayer1(m_pActivePopupTrack);
}

void Track::slotLoadPlayer2()
{
    slotLoadPlayer2(m_pActivePopupTrack);
}

