/*
    h5plotserie - plot the data of a hdf5 file.
    Copyright (C) 2010 Markus Schneider

    This program is free software; you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation; either version 2 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program; if not, write to the Free Software
    Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
*/

#include <QtGui>
#include "dataselection.h"

#include <hdf5serie/vectorserie.h>

#include "treewidgetitem.h"
#include "plotdata.h"
#include "curves.h"
#include "mainwindow.h"

DataSelection::DataSelection(QWidget * parent) : QSplitter(parent) {

  QWidget* dummy=new QWidget(this);
  addWidget(dummy);

  QGridLayout * fileSelection=new QGridLayout(this);
  dummy->setLayout(fileSelection);

  QLabel * filterLabel=new QLabel("Filter:");
  fileSelection->addWidget(filterLabel,0,0);
  filter=new QLineEdit(this);
  filter->setToolTip("Enter a regular expression to filter the list.");
  connect(filter, SIGNAL(returnPressed()), this, SLOT(filterObjectList()));
  fileSelection->addWidget(filter,0,1);

  fileBrowser = new QTreeWidget(this);
  fileSelection->addWidget(fileBrowser,1,0,1,2);
  fileBrowser->setHeaderHidden(true);
  fileBrowser->setColumnCount(1);

  QLabel * pathLabel=new QLabel("Path:");
  fileSelection->addWidget(pathLabel,2,0);
  path=new QLineEdit(this);
  fileSelection->addWidget(path,2,1);
  path->setReadOnly(true);

  currentData=new QListWidget(this);
  addWidget(currentData);

  //addFile("./MBS.mbsim.h5");
  //addFile("./MBS2.mbsim.h5");

  QObject::connect(fileBrowser, SIGNAL(itemClicked(QTreeWidgetItem*, int)), this, SLOT(selectFromFileBrowser(QTreeWidgetItem*,int)));
  QObject::connect(currentData, SIGNAL(itemClicked(QListWidgetItem*)), this, SLOT(selectFromCurrentData(QListWidgetItem*)));
  QObject::connect(fileBrowser,SIGNAL(currentItemChanged(QTreeWidgetItem *, QTreeWidgetItem *)), this, SLOT(updatePath(QTreeWidgetItem *)));
}

DataSelection::~DataSelection() {
  if (fileBrowser) {
    delete fileBrowser;
    fileBrowser=NULL;
  }
  if (filter) {
    delete filter;
    filter=NULL;
  }
  if (path) {
    delete path;
    path=NULL;
  }
}

void DataSelection::addFile(const QString &name) {
  fileInfo.append(name);
  file.append(new H5::H5File(name.toStdString(), H5F_ACC_RDONLY));
  TreeWidgetItem *topitem = new TreeWidgetItem(QStringList(fileInfo.back().fileName()));
  fileBrowser->addTopLevelItem(topitem);
  QList<QTreeWidgetItem *> items;
  for(unsigned int i=0; i<file.back()->getNumObjs(); i++)  {
    QTreeWidgetItem *item = new TreeWidgetItem(QStringList(file.back()->getObjnameByIdx(i).c_str()));
    H5::Group grp = file.back()->openGroup(file.back()->getObjnameByIdx(i));
    insertChildInTree(grp, item);
    topitem->addChild(item);
  }
}

void DataSelection::insertChildInTree(H5::Group &grp, QTreeWidgetItem *item) {
  for(unsigned int j=0; j<grp.getNumObjs(); j++) {
    QTreeWidgetItem *child = new TreeWidgetItem(QStringList(grp.getObjnameByIdx(j).c_str()));
    item->addChild(child);
    if(grp.getObjTypeByIdx(j)==0) {
      H5::Group childgrp = grp.openGroup(grp.getObjnameByIdx(j));
      insertChildInTree(childgrp, child);
    }
    else {
      if(grp.getObjnameByIdx(j) == "data") {
        QString path; 
        getPath(item,path,0);
        path += "/data";
        static_cast<TreeWidgetItem*>(child)->setPath(path);
      }
    }
  }
}

void DataSelection::getPath(QTreeWidgetItem* item, QString &s, int col) {
  QTreeWidgetItem* parentWidget = item->parent();
  if(parentWidget)
    getPath(parentWidget, s, col);
  s  += "/" + item->text(col);
}

void DataSelection::selectFromFileBrowser(QTreeWidgetItem* item, int col) {
  currentData->clear();
  if( item->text(col) == "data") {
    QString path = static_cast<TreeWidgetItem*>(item)->getPath();
    int j = getTopLevelIndex(item);
    H5::VectorSerie<double> vs;
    vs.open(*file[j], path.toStdString());
    QStringList sl;
    for(unsigned int i=0; i<vs.getColumns(); i++)
     sl << vs.getColumnLabel()[i].c_str();
    vs.close();
    currentData->addItems(sl);
  }
}

int DataSelection::getTopLevelIndex(QTreeWidgetItem* item) {
  QTreeWidgetItem* parentWidget = item->parent();
  if(parentWidget)
    return getTopLevelIndex(parentWidget);
  else // item is TopLevelItem
    return fileBrowser->indexOfTopLevelItem(item); 
}

void DataSelection::selectFromCurrentData(QListWidgetItem* item) {
  QString path = static_cast<TreeWidgetItem*>(fileBrowser->currentItem())->getPath();
  int col = currentData->row(item);
  int j = getTopLevelIndex(fileBrowser->currentItem());
  H5::VectorSerie<double> vs;
  vs.open(*file[j], path.toStdString());

  PlotData pd;
  pd.setValue("Filepath", fileInfo[j].absolutePath());
  pd.setValue("Filename", fileInfo[j].fileName());
  pd.setValue("x-Label", QString::fromStdString(vs.getColumnLabel()[0]));
  pd.setValue("y-Label", QString::fromStdString(vs.getColumnLabel()[col]));
  pd.setValue("offset", "0");
  pd.setValue("gain", "1");
  pd.setValue("x-Path", path);
  pd.setValue("y-Path", path);
  pd.setValue("x-Index", QString("%1").arg(0));
  pd.setValue("y-Index", QString("%1").arg(col));

  vs.close();

  if (QApplication::keyboardModifiers() & Qt::ShiftModifier)
    static_cast<MainWindow*>(parent()->parent())->getCurves()->modifyPlotData(pd, "add");
  else if (QApplication::keyboardModifiers() & Qt::ControlModifier)
    static_cast<MainWindow*>(parent()->parent())->getCurves()->modifyPlotData(pd, "new");
  else
    static_cast<MainWindow*>(parent()->parent())->getCurves()->modifyPlotData(pd, "replace");
}

// MainWindow::filterObjectList(...) and MainWindow::searchObjectList(...) are taken from OpenMBV.
// If changes are made here, please do the same changes in OpenMBV
void DataSelection::filterObjectList() {
  QRegExp filterRegExp(filter->text());
  searchObjectList(fileBrowser->invisibleRootItem(), filterRegExp);
}

void DataSelection::searchObjectList(QTreeWidgetItem *item, const QRegExp& filterRegExp) {
  for(int i=0; i<item->childCount(); i++) {
    // search recursive
    searchObjectList(item->child(i), filterRegExp);
    // set color
    QColor c=item->child(i)->foreground(0).color();
    c.setRed(filterRegExp.indexIn(item->child(i)->text(0))<0?255:0);
    item->child(i)->setForeground(0, QBrush(c));
    // if all children and children children are red, collapse
    int count=0;
    for(int j=0; j<item->child(i)->childCount(); j++)
      if((item->child(i)->child(j)->childCount()!=0 && item->child(i)->child(j)->foreground(0).color().red()==255 && 
            item->child(i)->child(j)->isExpanded()==false) ||
          (item->child(i)->child(j)->childCount()==0 && item->child(i)->child(j)->foreground(0).color().red()==255))
        count++;
    item->child(i)->setExpanded(count!=item->child(i)->childCount());
    // hide
    item->child(i)->setHidden(false);
    if((item->child(i)->childCount()!=0 && item->child(i)->foreground(0).color().red()==255 && 
          item->child(i)->isExpanded()==false) ||
        (item->child(i)->childCount()==0 && item->child(i)->foreground(0).color().red()==255)) {
      bool hide=true;
      for(QTreeWidgetItem *it=item; it!=0; it=it->parent())
        if(filterRegExp.indexIn(it->text(0))>=0) { hide=false; break; }
      item->child(i)->setHidden(hide);
    }
  }
}

void DataSelection::updatePath(QTreeWidgetItem *cur) {
  QString str=cur->text(0);
  for (QTreeWidgetItem *item=cur->parent(); item!=0; item=item->parent())
    str=item->text(0)+"/"+str;
  path->setText(str);
};