/* Copyright (C) 2009 Markus Friedrich
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301 USA
 *
 * Contact:
 *   friedrich.at.gc@googlemail.com
 *
 */

#include <config.h>
#include <hdf5serie/file.h>
#ifdef _WIN32
  #include <boost/interprocess/windows_shared_memory.hpp>
#else
  #include <boost/interprocess/shared_memory_object.hpp>
#endif
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/scoped_lock.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>
#include <boost/interprocess/sync/interprocess_condition.hpp>
#include <boost/lexical_cast.hpp>

using namespace std;
using namespace boost::interprocess;
using namespace boost::posix_time;
using namespace boost::filesystem;

namespace {
  void requestWriterFlush(H5::File::IPC &ipc, H5::File *me);
  void openIPC(H5::File::IPC &ipc, const boost::filesystem::path &filename);
  bool waitForWriterFlush(H5::File::IPC &ipc, H5::File *me);

  class RunAtExit {
    public:
      RunAtExit() = default;
      ~RunAtExit();
      void addIPCRemove(const string &interprocessName);
    private:
      set<string> ipcRemove;
  };
  RunAtExit runatexit;
}

namespace H5 {

int File::defaultCompression=1;
int File::defaultChunkSize=100;

set<File*> File::writerFiles;
set<File*> File::readerFiles;

File::File(const path &filename, FileAccess type_) : GroupBase(nullptr, filename.string()), type(type_), isSWMR(false) {
  file=this;
  open();

  interprocessName=canonical(filename).string();
  interprocessName="hdf5serie_"+to_string(hash<string>()(interprocessName));

  if(type==write) {
    writerFiles.insert(this);
    // remove interprocess elements
#ifndef _WIN32
    shared_memory_object::remove(interprocessName.c_str());
#endif
    // create interprocess elements
    ipc.filename=filename;
#ifdef _WIN32
    ipc.shm=std::make_shared<windows_shared_memory>(create_only, interprocessName.c_str(), read_write, sizeof(bool)+sizeof(interprocess_mutex)+sizeof(interprocess_condition));
#else
    ipc.shm=std::make_shared<shared_memory_object>(create_only, interprocessName.c_str(), read_write);
    ipc.shm->truncate(sizeof(bool)+sizeof(interprocess_mutex)+sizeof(interprocess_condition));
#endif
    ipc.shmmap=std::make_shared<mapped_region>(*ipc.shm, read_write);
    auto *ptr=static_cast<char*>(ipc.shmmap->get_address());
    ipc.flushVar=new(ptr) bool(false);               ptr+=sizeof(bool);
    ipc.mutex   =new(ptr) interprocess_mutex();      ptr+=sizeof(interprocess_mutex);
    ipc.cond    =new(ptr) interprocess_condition();//ptr+=sizeof(interprocess_condition);

    runatexit.addIPCRemove(interprocessName);
  }
  else {
    readerFiles.insert(this);
    // try to open interprocess elements
    openIPC(ipc, filename);
  }
}


File::~File() {
  if(type==write) {
    writerFiles.erase(this);
  }
  else
    readerFiles.erase(this);

  // remove interprocess elements
  ipc.shmmap.reset();
  ipc.shm.reset();
#ifndef _WIN32
  if(type==write)
    shared_memory_object::remove(interprocessName.c_str());
#endif
  for(auto & it : ipcAdd) {
    it.shmmap.reset();
    it.shm.reset();
#ifndef _WIN32
    if(type==write)
      shared_memory_object::remove(it.interprocessName.c_str());
#endif
  }

  close();
}

void File::reopenAsSWMR() {
  if(type==read)
    throw Exception(getPath(), "Can only reopen files opened for writing in SWMR mode");
  if(isSWMR) {
    msg(Warn)<<"reopenAsSWMR called more than once for file "<<name<<". Skipping this call (must be reworked after the HDF5 1.10 release)"<<endl;
    return;
  }

  isSWMR=true;

  close();
  open();
}

void File::reopenAllFilesAsSWMR() {
  for(auto writerFile : writerFiles)
    writerFile->reopenAsSWMR();
}

void File::refresh() {
  if(type==write)
    throw Exception(getPath(), "refresh() can only be called for reading files");

  // refresh file
#if H5_VERSION_GE(1, 10, 0)
  GroupBase::refresh();
#else
  close();
  open();
#endif
}

void File::flush() {
  if(type==read)
    throw Exception(getPath(), "flush() can only be called for writing files");

#if H5_VERSION_GE(1, 10, 0)
  GroupBase::flush();
#else
  H5Fflush(id, H5F_SCOPE_GLOBAL);
#endif
}

void File::close() {
  // close everything (except the file itself)
  GroupBase::close();

  // check if all object are closed now: if not -> throw internal error (with details about the opened objects)
  ssize_t count=H5Fget_obj_count(id, H5F_OBJ_DATASET | H5F_OBJ_GROUP | H5F_OBJ_DATATYPE | H5F_OBJ_ATTR | H5F_OBJ_LOCAL);
  if(count<0)
    throw Exception(getPath(), "Internal error: H5Fget_obj_count failed");
  if(count>0) {
    vector<hid_t> obj(count, 0);
    ssize_t ret=H5Fget_obj_ids(id, H5F_OBJ_DATASET | H5F_OBJ_GROUP | H5F_OBJ_DATATYPE | H5F_OBJ_ATTR | H5F_OBJ_LOCAL, count, &obj[0]);
    if(ret<0)
      throw Exception(getPath(), "Internal error: H5Fget_obj_ids failed");
    vector<char> name(1000+1);
    stringstream err;
    err<<"Internal error: Can not close file since "<<count<<" elements are still open:"<<endl;
    for(auto it : obj) {
      size_t ret=H5Iget_name(it, &name[0], 1000);
      if(ret<=0)
        throw Exception(getPath(), "Internal error: H5Iget_name");
      err<<"type="<<H5Iget_type(it)<<" name="<<(ret>0?&name[0]:"<no name>")<<endl;
    }
    throw Exception(getPath(), err.str());
  }

  // now close also the file with is now the last opened identifier
  id.reset();
}

void File::open() {
  if(type==write) {
    if(!isSWMR) {
      ScopedHID faid(H5Pcreate(H5P_FILE_ACCESS), &H5Pclose);
      H5Pset_libver_bounds(faid, H5F_LIBVER_LATEST, H5F_LIBVER_LATEST);
      id.reset(H5Fcreate(name.c_str(), H5F_ACC_TRUNC, H5P_DEFAULT, faid), &H5Fclose);
    }
    else {
      unsigned int flag=H5F_ACC_RDWR;
#if H5_VERSION_GE(1, 10, 0)
      flag|=H5F_ACC_SWMR_WRITE;
#endif
      id.reset(H5Fopen(name.c_str(), flag, H5P_DEFAULT), &H5Fclose);
    }
  }
  else {
    unsigned int flag=H5F_ACC_RDONLY;
#if H5_VERSION_GE(1, 10, 0)
    flag|=H5F_ACC_SWMR_READ;
#endif
    id.reset(H5Fopen(name.c_str(), flag, H5P_DEFAULT), &H5Fclose);
  }
  GroupBase::open();
}



void File::flushIfRequested() {
  bool localFlushVar;
  {
    boost::interprocess::scoped_lock<interprocess_mutex> lock(*ipc.mutex);
    localFlushVar=*ipc.flushVar;
  }
  if(localFlushVar) {
    if(msgAct(Debug))
      msg(Debug)<<"Flushing HDF5 file "+name+", requested by reader process, and send notification if flush finished."<<endl;
    flush();
    boost::interprocess::scoped_lock<interprocess_mutex> lock(*ipc.mutex);
    *ipc.flushVar=false;
    ipc.cond->notify_all();
  }
}

void File::flushAllFiles() {
  for(auto writerFile : writerFiles)
    writerFile->flush();
}

void File::flushAllFilesIfRequested() {
  for(auto writerFile : writerFiles)
    writerFile->flushIfRequested();
}

void File::refreshAfterWriterFlush() {
  requestWriterFlush();
  if(waitForWriterFlush())
    refresh();
}

void File::refreshAllFiles() {
  for(auto readerFile : readerFiles)
    readerFile->refresh();
}

void File::refreshFilesAfterWriterFlush(const std::set<File*> &files) {
  for(auto file : files)
    file->requestWriterFlush();
  vector<bool> refreshNeeded;
  refreshNeeded.reserve(files.size());
  for(auto file : files)
    refreshNeeded.push_back(file->waitForWriterFlush());
  auto nit=refreshNeeded.begin();
  for(auto it=files.begin(); it!=files.end(); ++it, ++nit)
    if(*nit)
      (*it)->refresh();
}

void File::refreshAllFilesAfterWriterFlush() {
  refreshFilesAfterWriterFlush(readerFiles);
}

void File::requestWriterFlush() {
  ::requestWriterFlush(ipc, this);
  // post also files with are linked by this file
  for(auto & it : ipcAdd)
    ::requestWriterFlush(it, this);
}

bool File::waitForWriterFlush() {
  bool ret=::waitForWriterFlush(ipc, this);
  // wait also for files with are linked by this file
  for(auto & it : ipcAdd)
    if(::waitForWriterFlush(it, this))
      ret=true;
  return ret;
}

void File::addFileToNotifyOnRefresh(const boost::filesystem::path &filename) {
  IPC ipc;
  openIPC(ipc, filename);
  if(!ipc.shm)
    return;
  ipcAdd.push_back(ipc);
}

}

namespace {

void requestWriterFlush(H5::File::IPC &ipc, H5::File *me) {
  if(!ipc.shm)
    return;
  if(me->msgAct(me->Debug))
    me->msg(me->Debug)<<"Ask writer process to flush hdf5 file "<<ipc.filename.string()<<"."<<endl;
  // post this file
  {
    boost::interprocess::scoped_lock<interprocess_mutex> lock(*ipc.mutex);
    *ipc.flushVar=true;
  }
  // save current time for later use in timed_wait
  ipc.flushRequestTime=microsec_clock::universal_time();
}

bool waitForWriterFlush(H5::File::IPC &ipc, H5::File *me) {
  if(!ipc.shm)
    return false;
  // get time to wait
  int msec=1000/25; // default wait time is 1/25Hz
  static char *waitTime=getenv("HDF5SERIE_REFRESHWAITTIME");
  if(waitTime)
    msec=boost::lexical_cast<int>(waitTime);
  // wait for file
  bool flushReady=true;
  {
    boost::interprocess::scoped_lock<interprocess_mutex> lock(*ipc.mutex);
    if(*ipc.flushVar) {
      flushReady=ipc.cond->timed_wait(lock, ipc.flushRequestTime+milliseconds(msec));//MFMF not working; never timeout out
    }
  }
  // print message
  if(flushReady) {
    time_duration d=microsec_clock::universal_time()-ipc.flushRequestTime;
    if(me->msgAct(me->Debug))
      me->msg(me->Debug)<<"Flush of writer succsessfull after "<<d.total_milliseconds()<<" msec. Using newest data now."<<endl;
  }
  else
    if(me->msgAct(me->Warn))
      me->msg(me->Warn)<<"Writer process has not flushed hdf5 file "<<ipc.filename.string()<<" after "<<msec<<" msec, continue with maybe not newest data."<<endl;

  return true;
}

void openIPC(H5::File::IPC &ipc, const path &filename) {
  string interprocessName=canonical(filename).string();
  interprocessName="hdf5serie_"+to_string(hash<string>()(interprocessName));
  try {
    ipc.filename=filename;
    ipc.interprocessName=interprocessName;
#ifdef _WIN32
    ipc.shm=std::make_shared<windows_shared_memory>(open_only, interprocessName.c_str(), read_write);
#else
    ipc.shm=std::make_shared<shared_memory_object>(open_only, interprocessName.c_str(), read_write);
#endif
    ipc.shmmap=std::make_shared<mapped_region>(*ipc.shm, read_write);
    auto *ptr=static_cast<char*>(ipc.shmmap->get_address());
    ipc.flushVar=reinterpret_cast<bool*>                  (ptr);  ptr+=sizeof(bool);
    ipc.mutex   =reinterpret_cast<interprocess_mutex*>    (ptr);  ptr+=sizeof(interprocess_mutex);
    ipc.cond    =reinterpret_cast<interprocess_condition*>(ptr);//ptr+=sizeof(interprocess_condition);
  }
  catch(const interprocess_exception &ex) {
    ipc.shm.reset();
    ipc.shmmap.reset();
  }
}

RunAtExit::~RunAtExit() {
#ifndef _WIN32
  for(const auto & it : ipcRemove)
    shared_memory_object::remove(it.c_str());
#endif
}

void RunAtExit::addIPCRemove(const string &interprocessName) {
  ipcRemove.insert(interprocessName);
}

}
