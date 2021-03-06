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

#include <config.h>
#include "QApplication"
#include "QGridLayout"
#include "QLabel"
#include "QLineEdit"
#include "QListWidget"
#include "QFileInfo"
#include "dataselection.h"

#include <hdf5serie/vectorserie.h>

#include "treewidgetitem.h"
#include "plotdata.h"
#include "curves.h"
#include "mainwindow.h"

using namespace std;

DataSelection::DataSelection(QWidget * parent) : QSplitter(parent) {

  QWidget* dummy=new QWidget(this);
  addWidget(dummy);

  auto * fileSelection=new QGridLayout(this);
  dummy->setLayout(fileSelection);

  QLabel * filterLabel=new QLabel("Filter:");
  fileSelection->addWidget(filterLabel,0,0);
  filter=new QLineEdit(this);
  filter->setToolTip("Enter a regular expression to filter the list.");
  connect(filter, &QLineEdit::returnPressed, this, &DataSelection::filterObjectList);
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

  QObject::connect(fileBrowser, &QTreeWidget::itemClicked, this, &DataSelection::selectFromFileBrowser);
  QObject::connect(currentData, &QListWidget::itemClicked, this, &DataSelection::selectFromCurrentData);
  QObject::connect(fileBrowser,&QTreeWidget::currentItemChanged, this, &DataSelection::updatePath);
}

DataSelection::~DataSelection() {
  if (fileBrowser) {
    delete fileBrowser;
    fileBrowser=nullptr;
  }
  if (filter) {
    delete filter;
    filter=nullptr;
  }
  if (path) {
    delete path;
    path=nullptr;
  }
}

void DataSelection::addFile(const QString &name) {
  auto it=find_if(h5File.begin(), h5File.end(), [&name](const decltype(h5File)::value_type& a){
    return boost::filesystem::equivalent(a.first, name.toStdString());
  });
  if(it!=h5File.end()) {
    it->second->refreshAfterWriterFlush();
    return;
  }

  fileInfo.append(name);
  file.append(name);
  std::shared_ptr<H5::File> h5f;
  h5f=std::make_shared<H5::File>(file.back().toStdString(), H5::File::read);
  h5File.emplace_back(name.toStdString(), h5f);

  TreeWidgetItem *topitem = new TreeWidgetItem(QStringList(fileInfo.back().fileName()));
  topitem->setToolTip(0, fileInfo.back().absoluteFilePath());
  fileBrowser->addTopLevelItem(topitem);
  QList<QTreeWidgetItem *> items;
  set<string> names=h5f->getChildObjectNames();
  for(const auto & name : names) {
    QTreeWidgetItem *item = new TreeWidgetItem(QStringList(name.c_str()));
    auto *grp = h5f->openChildObject<H5::Group>(name);
    insertChildInTree(grp, item);
    topitem->addChild(item);
  }
  h5f->refreshAfterWriterFlush();
}

shared_ptr<H5::File> DataSelection::getH5File(const boost::filesystem::path &p) const {
  auto it=find_if(h5File.begin(), h5File.end(), [&p](const decltype(h5File)::value_type& a){
    return boost::filesystem::equivalent(a.first, p);
  });
  if(it==h5File.end())
    throw runtime_error("Cannot find "+p.string()+" in h5File.");
  return it->second;
}

void DataSelection::insertChildInTree(H5::Group *grp, QTreeWidgetItem *item) {
  set<string> names=grp->getChildObjectNames();
  for(const auto & name : names) {
    QTreeWidgetItem *child = new TreeWidgetItem(QStringList(name.c_str()));
    item->addChild(child);
    auto *g=dynamic_cast<H5::Group*>(grp->openChildObject(name));
    if(g)
      insertChildInTree(g, child);
    else {
      if(name == "data") {
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
    std::shared_ptr<H5::File> h5f=getH5File(file[j].toStdString());
    auto *vs=h5f->openChildObject<H5::VectorSerie<double> >(path.toStdString());
    QStringList sl;
    for(unsigned int i=0; i<vs->getColumns(); i++)
      sl << vs->getColumnLabel()[i].c_str();
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
  std::shared_ptr<H5::File> h5f=getH5File(file[j].toStdString());
  auto *vs=h5f->openChildObject<H5::VectorSerie<double> >(path.toStdString());

  PlotData pd;
  pd.setValue("Filepath", fileInfo[j].absolutePath());
  pd.setValue("Filename", fileInfo[j].fileName());
  pd.setValue("x-Label", QString::fromStdString(vs->getColumnLabel()[0]));
  pd.setValue("y-Label", QString::fromStdString(vs->getColumnLabel()[col]));
  pd.setValue("offset", "0");
  pd.setValue("gain", "1");
  pd.setValue("x-Path", path);
  pd.setValue("y-Path", path);
  pd.setValue("x-Index", QString("%1").arg(0));
  pd.setValue("y-Index", QString("%1").arg(col));

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
    ((TreeWidgetItem*)item->child(i))->setSearchMatched(filterRegExp.indexIn(item->child(i)->text(0))>=0);
    ((TreeWidgetItem*)item->child(i))->updateTextColor();
    // if all children and children children are red, collapse
    int count=0;
    for(int j=0; j<item->child(i)->childCount(); j++)
      if((item->child(i)->child(j)->childCount()!=0 && !(((TreeWidgetItem*)(item->child(i)->child(j)))->getSearchMatched()) && 
            !item->child(i)->child(j)->isExpanded()) ||
          (item->child(i)->child(j)->childCount()==0 && !(((TreeWidgetItem*)(item->child(i)->child(j)))->getSearchMatched())))
        count++;
    item->child(i)->setExpanded(count!=item->child(i)->childCount());
    // hide
    item->child(i)->setHidden(false);
    if((item->child(i)->childCount()!=0 && !(((TreeWidgetItem*)(item->child(i)))->getSearchMatched()) && 
          !item->child(i)->isExpanded()) ||
        (item->child(i)->childCount()==0 && !(((TreeWidgetItem*)(item->child(i)))->getSearchMatched()))) {
      bool hide=true;
      for(QTreeWidgetItem *it=item; it!=nullptr; it=it->parent())
        if(filterRegExp.indexIn(it->text(0))>=0) { hide=false; break; }
      item->child(i)->setHidden(hide);
    }
  }
}

void DataSelection::updatePath(QTreeWidgetItem *cur) {
  QString str=cur->text(0);
  for (QTreeWidgetItem *item=cur->parent(); item!=nullptr; item=item->parent())
    str=item->text(0)+"/"+str;
  path->setText(str);
};
