/* ###
 * IP: GHIDRA
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 *
 * Modified for SaturnExplorer: the destructor frees the return-recovery state (patches/0004-funcdata-frees-active-output.patch).
 * Modified for SaturnExplorer: every LOAD and STORE's fate is recorded (patches/0017-access-fates.patch).
 * Modified for SaturnExplorer: the instructions and jump tables the flow recovered are recorded (patches/0021-flow-instructions.patch).
 * Modified for SaturnExplorer: an access folded into the stack frame is flagged, and recorded again if a write goes with its value unread or the access with an unreachable block (patches/0068-engine-deadcode-late-stack-reads.patch).
 */
#include "funcdata.hh"

namespace ghidra {

AttributeId ATTRIB_NOCODE = AttributeId("nocode",84);

ElementId ELEM_AST = ElementId("ast",115);
ElementId ELEM_FUNCTION = ElementId("function",116);
ElementId ELEM_HIGHLIST = ElementId("highlist",117);
ElementId ELEM_JUMPTABLELIST = ElementId("jumptablelist",118);
ElementId ELEM_VARNODES = ElementId("varnodes",119);

/// \param nm is the (base) name of the function, as a formal symbol
/// \param disp is the name used when displaying the function name in output
/// \param scope is Symbol scope associated with the function
/// \param addr is the entry address for the function
/// \param sym is the symbol representing the function
/// \param sz is the number of bytes (of code) in the function body
Funcdata::Funcdata(const string &nm,const string &disp,Scope *scope,const Address &addr,FunctionSymbol *sym,int4 sz)
  : baseaddr(addr),
    funcp(),
    vbank(scope->getArch()),
    heritage(this),
    covermerge(*this)

{				// Initialize high-level properties of
				// function by giving address and size
  functionSymbol = sym;
  flags = 0;
  clean_up_index = 0;
  high_level_index = 0;
  cast_phase_index = 0;
  glb = scope->getArch();
  minLanedSize = glb->getMinimumLanedRegisterSize();
  name = nm;
  displayName = disp;

  size = sz;
  AddrSpace *stackid = glb->getStackSpace();
  if (nm.size()==0)
    localmap = (ScopeLocal *)0; // Filled in by decode
  else {
    uint8 id;
    if (sym != (FunctionSymbol *)0)
      id = sym->getId();
    else {
      // Missing a symbol, build unique id based on address
      id = 0x57AB12CD;
      id = (id << 32) | (addr.getOffset() & 0xffffffff);
    }
    ScopeLocal *newMap = new ScopeLocal(id,stackid,this,glb);
    glb->symboltab->attachScope(newMap,scope);		// This may throw and delete newMap
    localmap = newMap;
    funcp.setScope(localmap,baseaddr+ -1);
    localmap->resetLocalWindow();
  }
  activeoutput = (ParamActive *)0;

#ifdef OPACTION_DEBUG
  jtcallback = (void (*)(Funcdata &orig,Funcdata &fd))0;
  opactdbg_count = 0;
  opactdbg_breakcount = -1;
  opactdbg_on = false;
  opactdbg_breakon = false;
  opactdbg_active = false;
#endif
}

void Funcdata::clear(void)

{				// Clear everything associated with decompilation (analysis)

  flags &= ~(highlevel_on|blocks_generated|processing_started|typerecovery_start|typerecovery_on|
      double_precis_on|restart_pending|normalization_on);
  clean_up_index = 0;
  high_level_index = 0;
  cast_phase_index = 0;
  minLanedSize = glb->getMinimumLanedRegisterSize();

  localmap->clearUnlocked();	// Clear non-permanent stuff
  localmap->resetLocalWindow();

  clearActiveOutput();
  funcp.clearUnlockedOutput();	// Inputs are cleared by localmap
  unionMap.clear();
  clearBlocks();
  obank.clear();
  vbank.clear();
  clearCallSpecs();
  clearJumpTables();
  accessFates.clear();		// SaturnExplorer: they describe the ops just destroyed
  frameEscaped = false;		// SaturnExplorer (0068): ...and the escape found in them
  flowInsns.clear();		// SaturnExplorer: the next flow records its own
  flowTables.clear();
  flowOps = 0;
  // Do not clear overrides
  heritage.clear();
  covermerge.clear();
#ifdef OPACTION_DEBUG
  opactdbg_count = 0;
#endif
}

/// SaturnExplorer: is the op one of the function's memory accesses? A LOAD or STORE is. So is a
/// volatile read or write user op, which replaceVolatile makes of a LOAD or STORE at a volatile
/// address; and so, in the time between RuleLoadVarnode or RuleStoreVarnode and replaceVolatile,
/// is the COPY of a volatile Varnode they leave. A COPY of anything else is not.
/// \param op is the given op
/// \param store is set to \b true if it writes memory
/// \param size is set to the number of bytes accessed
/// \param addr is set to the constant address accessed, or an invalid Address if the pointer is not constant
/// \return \b true if the op is a memory access
bool Funcdata::isMemoryAccess(const PcodeOp *op,bool &store,int4 &size,Address &addr) const

{
  addr = Address();
  switch(op->code()) {
  case CPUI_LOAD:
    store = false;
    size = (op->getOut() != (Varnode *)0) ? op->getOut()->getSize() : 0;
    break;
  case CPUI_STORE:
    store = true;
    size = (op->numInput() > 2 && op->getIn(2) != (Varnode *)0) ? op->getIn(2)->getSize() : 0;
    break;
  case CPUI_CALLOTHER:
    {
      // An input can be missing: an op in a block being removed has lost those that came from ops
      // destroyed before it. blockRemoveInternal records its accesses first for that reason.
      if (op->numInput() < 2 || op->getIn(0) == (Varnode *)0 || !op->getIn(0)->isConstant()) return false;
      const UserPcodeOp *u = glb->userops.getOp((uint4)op->getIn(0)->getOffset());
      if (u == (const UserPcodeOp *)0) return false;
      if (u->getType() == UserPcodeOp::volatile_read) {
	store = false;
	size = (op->getOut() != (Varnode *)0) ? op->getOut()->getSize() : 0;
      }
      else if (u->getType() == UserPcodeOp::volatile_write) {
	store = true;
	size = (op->numInput() > 2 && op->getIn(2) != (Varnode *)0) ? op->getIn(2)->getSize() : 0;
      }
      else
	return false;
      if (op->getIn(1) != (Varnode *)0)
	addr = op->getIn(1)->getAddr();
      return true;
    }
  case CPUI_COPY:
    {
      const Varnode *vn = (op->numInput() > 0) ? op->getIn(0) : (const Varnode *)0;
      const Varnode *outvn = op->getOut();
      if (vn == (const Varnode *)0 || outvn == (const Varnode *)0) return false;
      if (vn->isVolatile() && !vn->isConstant() && !(vn->isReadOnly() && glb->readonlypropagate)) {
	store = false;		// A read: the COPY's input is the volatile location
	size = vn->getSize();
	addr = vn->getAddr();
	return true;
      }
      if (outvn->isVolatile() && !(outvn->isReadOnly() && glb->readonlypropagate)) {
	store = true;		// A write: the COPY's output is
	size = outvn->getSize();
	addr = outvn->getAddr();
	return true;
      }
      return false;
    }
  default:
    return false;
  }
  if (op->numInput() > 1 && op->getIn(0) != (Varnode *)0 && op->getIn(1) != (Varnode *)0 && op->getIn(1)->isConstant())
    addr = Address(op->getIn(0)->getSpaceFromConst(),op->getIn(1)->getOffset());
  return true;
}

/// SaturnExplorer: a host that shows a function's memory accesses accounts for each one the flow
/// made by this record. It is made before anything is changed, so an allocation failure here
/// leaves the op as it was.
/// \param op is the memory access, still whole
/// \param store is \b true if it writes memory
/// \param size is the number of bytes accessed
/// \param addr is the constant address accessed, or an invalid Address
/// \param fate is how it ended
void Funcdata::recordAccessFate(const PcodeOp *op,bool store,int4 size,const Address &addr,AccessFate fate)

{
  // Only the flow's accesses are accounted for. A user op for a volatile access is made by
  // replaceVolatile, after the flow, from a LOAD or STORE (or the COPY it became) that was the flow's.
  // Loads and stores that rules and actions make (a call's stack parameters, say) are not the code's.
  if (op->code() != CPUI_CALLOTHER && op->getTime() >= flowOps)
    return;
  AccessRecord rec;
  rec.time = op->getTime();
  rec.pc = op->getAddr();
  rec.addr = addr;
  rec.store = store;
  rec.size = size;
  rec.fate = fate;
  accessFates.push_back(rec);
}

/// SaturnExplorer: RuleLoadVarnode and RuleStoreVarnode turn a LOAD or STORE at a constant address
/// into a COPY of the Varnode there. If that memory is volatile the COPY is replaced later by a user
/// op (replaceVolatile), which is a real access and stays in the p-code, so nothing is recorded.
/// Otherwise the access is now an ordinary variable that data-flow may forward or remove.
/// \param op is the LOAD or STORE, not yet changed
/// \param vn is the Varnode at the address accessed
void Funcdata::recordAccessFold(const PcodeOp *op,const Varnode *vn)

{
  if (vn->isVolatile() && !(vn->isReadOnly() && glb->readonlypropagate))
    return;
  bool store;
  int4 size;
  Address addr;
  if (!isMemoryAccess(op,store,size,addr)) return;
  recordAccessFate(op,store,size,vn->getAddr(),fate_folded_ordinary);
  // (0068) A flow LOAD or STORE of the stack frame lives on as a variable read or write. Its fold is no
  // conservation unless the value reaches what reads it: the op is flagged, a reader that stays and is given
  // another value in its place forwards it (noteFrameFoldForwarded), and opDestroy records the op again
  // (fate_frame_unread) if it goes with its value never forwarded (A3-124's stores, removed before the read
  // was found, or with the pieces of a wrong collapse, were counted as folded and nothing more).
  if (op->getTime() < flowOps && vn->getSpace()->getType() == IPTR_SPACEBASE)
    op->setFrameFold();
}

/// SaturnExplorer (patches/0068-engine-deadcode-late-stack-reads.patch): a LOAD or STORE that
/// RuleLoadVarnode or RuleStoreVarnode folded into the stack frame ends a second time. With
/// \b fate_frame_unread, the op is being destroyed and its value was never forwarded (noteFrameFoldForwarded):
/// every op that read it, directly or through MULTIEQUALs and INDIRECTs, was destroyed as dead. A host
/// compares a write with the reads of the frame the code makes and the calls that may read it: one that
/// reads a byte of it later, with no write of the frame between, read a value the analysis dropped; a read
/// whose value nothing used (a register pop the engine takes for unaffected) is no such read. With \b fate_frame_removed, the read or write
/// went with an unreachable block, so it is no read of the frame the code makes. The record repeats
/// the frame offset and size of the fold (the COPY's output may have been replaced since).
/// \param op is the COPY the LOAD or STORE became, still whole
/// \param fate is how it ended
void Funcdata::recordFrameEnd(const PcodeOp *op,AccessFate fate)

{
  for(vector<AccessRecord>::const_iterator iter=accessFates.begin();iter!=accessFates.end();++iter) {
    if ((*iter).time != op->getTime() || (*iter).fate != fate_folded_ordinary) continue;
    AccessRecord rec(*iter);
    rec.fate = fate;
    accessFates.push_back(rec);
    return;
  }
}

/// The comment is added to the global database, indexed via its placement address and
/// the entry address of the function. The emitter will attempt to place the comment
/// before the source expression that maps most closely to the address.
/// \param txt is the string body of the comment
/// \param ad is the placement address
void Funcdata::warning(const string &txt,const Address &ad) const

{
  string msg;
  if ((flags & jumptablerecovery_on)!=0)
    msg = "WARNING (jumptable): ";
  else
    msg = "WARNING: ";
  msg += txt;
  glb->commentdb->addCommentNoDuplicate(Comment::warning,baseaddr,ad,msg);
}

/// The warning will be emitted as part of the block comment printed right before the
/// prototype. The comment is stored in the global comment database, indexed via the function's
/// entry address.
/// \param txt is the string body of the comment
void Funcdata::warningHeader(const string &txt) const

{
  string msg;
  if ((flags & jumptablerecovery_on)!=0)
    msg = "WARNING (jumptable): ";
  else
    msg = "WARNING: ";
  msg += txt;
  glb->commentdb->addCommentNoDuplicate(Comment::warningheader,baseaddr,baseaddr,msg);
}

/// This routine does basic set-up for analyzing the function. In particular, it
/// generates the raw p-code, builds basic blocks, and generates the call specification
/// objects.
void Funcdata::startProcessing(void)

{
  if ((flags & processing_started)!=0)
    throw LowlevelError("Function processing already started");
  flags |= processing_started;

  if (funcp.isInline())
    warningHeader("This is an inlined function");
  localmap->clearUnlocked();
  funcp.clearUnlockedOutput();
  Address baddr(baseaddr.getSpace(),0);
  Address eaddr(baseaddr.getSpace(),~((uintb)0));
  followFlow(baddr,eaddr);
  structureReset();
  sortCallSpecs();		// Must come after structure reset
  heritage.buildInfoList();
  localoverride.applyDeadCodeDelay(*this);
}

void Funcdata::stopProcessing(void)

{
  flags |= processing_complete;
  obank.destroyDead();		// Free up anything in the dead list
#ifdef CPUI_STATISTICS
  glb->stats->process(*this);
#endif
}

bool Funcdata::startTypeRecovery(void)

{
  if ((flags & typerecovery_start)!=0) return false; // Already started
  flags |= typerecovery_start;
  return true;
}

Funcdata::~Funcdata(void)

{
  //  clear();
  if (localmap != (ScopeLocal *)0)
    glb->symboltab->deleteScope(localmap);

  clearCallSpecs();
  for(int4 i=0;i<jumpvec.size();++i) // Delete jumptables
    delete jumpvec[i];
  // An analysis abandoned between ActionPrototypeTypes and return recovery (a cancel, an error)
  // still owns its ParamActive: free it here, as normal completion does.
  clearActiveOutput();
  glb = (Architecture *)0;
}

/// A representation of all PcodeOps in the function body are printed to the
/// stream. Depending on the state of analysis, PcodeOps are grouped into their
/// basic blocks, and within a block, ops are displayed sequentially. Basic labeling
/// of branch destinations is also printed.  This is suitable for a console mode or
/// debug view of the state of the function at any given point in its analysis.
/// \param s is the output stream
void Funcdata::printRaw(ostream &s) const

{
  if (bblocks.getSize()==0) {
    if (obank.empty())
      throw RecovError("No operations to print");
    PcodeOpTree::const_iterator iter;
    s << "Raw operations: \n";
    for(iter=obank.beginMain();iter!=obank.endMain();++iter) {
      s << (*iter).second->getSeqNum() << ":\t";
      (*iter).second->printRaw(s);
      s << endl;
    }
  }
  else
    bblocks.printRaw(s);
}

/// This routine searches for an marks Varnode objects, like stack-pointer registers,
/// that are used as a base address for a virtual address space. Each Varnode gets a
/// special data-type and is marked so that Varnode::isSpacebase() returns \b true.
void Funcdata::spacebase(void)

{
  VarnodeLocSet::const_iterator iter,enditer;
  int4 i,j,numspace;
  Varnode *vn;
  AddrSpace *spc;

  for(j=0;j<glb->numSpaces();++j) {
    spc = glb->getSpace(j);
    if (spc == (AddrSpace *)0) continue;
    numspace = spc->numSpacebase();
    for(i=0;i<numspace;++i) {
      const VarnodeData &point(spc->getSpacebase(i));
				// Find input varnode at this size and location
      Datatype *ct = glb->types->getTypeSpacebase(spc,localmap->getId());
      Datatype *ptr = glb->types->getTypePointer(point.size,ct,spc->getWordSize());
    
      iter = vbank.beginLoc(point.size,Address(point.space,point.offset));
      enditer = vbank.endLoc(point.size,Address(point.space,point.offset));
      while(iter != enditer) {
	vn = *iter++;
	if (vn->isFree()) continue;
	if (vn->isSpacebase()) { // This has already been marked spacebase
				// We have given it a chance for descendants to
				// be eliminated naturally, now force a split if
				// it still has multiple descendants
	  PcodeOp *op = vn->getDef();
	  if ((op != (PcodeOp *)0)&&(op->code() == CPUI_INT_ADD))
	    splitUses(vn);
	}
	else {
	  vn->setFlags(Varnode::spacebase); // Mark all base registers (not just input)
	  if (vn->isInput())	// Only set type on the input spacebase register
	    vn->updateType(ptr,true,true);
	}
      }
    }
  }
}

/// Given an address space, like \e stack, that is known to have a base register
/// pointing to it, construct a Varnode representing that register.
/// \param id is the \e stack like address space
/// \return a newly allocated stack-pointer Varnode
Varnode *Funcdata::newSpacebasePtr(AddrSpace *id)

{
  Varnode *vn;

  // Assume that id has a base register (otherwise an exception is thrown)
  const VarnodeData &point(id->getSpacebase(0));
  vn = newVarnode(point.size, Address(point.space,point.offset));
  return vn;
}

/// Given an address space, like \e stack, that is known to have a base register
/// pointing to it, try to locate the unique Varnode that holds the input value
/// of this register.
/// \param id is the \e stack like address space
/// \return the input stack-pointer Varnode (or NULL if it doesn't exist)
Varnode *Funcdata::findSpacebaseInput(AddrSpace *id) const

{
  Varnode *vn;

  // Assume that id has a base register (otherwise an exception is thrown)
  const VarnodeData &point(id->getSpacebase(0));
  vn = vbank.findInput(point.size, Address(point.space,point.offset));
  return vn;
}

/// \brief If it doesn't exist, create an input Varnode of the base register corresponding to the given address space
///
/// The address space must have a base register associated with it or an exception is thrown.
/// If a Varnode representing the incoming base register already exists, it is returned. Otherwise
/// a new Varnode is created and returned.  In either case, the Varnode will have the TypeSpacebase data-type set.
/// \param id is the given address space
/// \return the input Varnode corresponding to the base register
Varnode *Funcdata::constructSpacebaseInput(AddrSpace *id)

{
  Varnode *spacePtr = findSpacebaseInput(id);
  if (spacePtr != (Varnode *)0)
    return spacePtr;
  if (id->numSpacebase() == 0)
    throw LowlevelError("Unable to construct pointer into space: "+id->getName());
  const VarnodeData &point(id->getSpacebase(0));
  Datatype *ct = glb->types->getTypeSpacebase(id,localmap->getId());
  Datatype *ptr = glb->types->getTypePointer(point.size,ct,id->getWordSize());
  spacePtr = newVarnode(point.size, point.getAddr(), ptr);
  spacePtr = setInputVarnode(spacePtr);
  spacePtr->setFlags(Varnode::spacebase);
  spacePtr->updateType(ptr, true, true);
  return spacePtr;
}

/// \brief Create a constant representing the \e base of the given global address space
///
/// The constant will have the TypeSpacebase data-type set.
/// \param id is the given address space
/// \return the constant base Varnode
Varnode *Funcdata::constructConstSpacebase(AddrSpace *id)

{
  Datatype *ct = glb->types->getTypeSpacebase(id,0);
  Datatype *ptr = glb->types->getTypePointer(id->getAddrSize(),ct,id->getWordSize());
  Varnode *spacePtr = newConstant(id->getAddrSize(),0);
  spacePtr->updateType(ptr,true,true);
  spacePtr->setFlags(Varnode::spacebase);
  return spacePtr;
}

/// \brief Convert a constant pointer into a \e ram CPUI_PTRSUB
///
/// A constant known to be a pointer into an address space like \b ram is converted
/// into a Varnode defined by CPUI_PTRSUB, which triggers a Symbol lookup at points
/// during analysis.  The constant must point to a known Symbol.
///
/// The PTRSUB takes the constant 0 as its first input, which is marked
/// as a \e spacebase to indicate this situation. The second input to PTRSUB becomes
/// the offset to the Symbol within the address space. An additional INT_SUB may be inserted
/// to get from the start of the Symbol to the address indicated by the original
/// constant pointer.
/// \param op is the PcodeOp referencing the constant pointer
/// \param slot is the input slot of the constant pointer
/// \param entry is the Symbol being pointed (in)to
/// \param rampoint is the constant pointer interpreted as an Address
/// \param origval is the constant
/// \param origsize is the size of the constant
void Funcdata::spacebaseConstant(PcodeOp *op,int4 slot,MapEntry *entry,const Address &rampoint,uintb origval,int4 origsize)

{
  int4 sz = rampoint.getAddrSize();
  AddrSpace *spaceid = rampoint.getSpace();
  Datatype *sb_type = glb->types->getTypeSpacebase(spaceid,entry->getSymbol()->getScope()->getId());
  sb_type = glb->types->getTypePointer(sz,sb_type,spaceid->getWordSize());
  Varnode *spacebase_vn,*outvn,*newconst;

  uintb extra = rampoint.getOffset() - entry->getAddr().getOffset();		// Offset from beginning of entry
  extra = AddrSpace::byteToAddress(extra,rampoint.getSpace()->getWordSize());	// Convert to address units

  PcodeOp *addOp = (PcodeOp *)0;
  PcodeOp *extraOp = (PcodeOp *)0;
  PcodeOp *zextOp = (PcodeOp *)0;
  PcodeOp *subOp = (PcodeOp *)0;
  bool isCopy = false;
  if (op->code() == CPUI_COPY) {	// We replace COPY with final op of this calculation
    isCopy = true;
    if (sz < origsize)
      zextOp = op;
    else {
      op->insertInput(1);	// PTRSUB, ADD, SUBPIECE all take 2 parameters
      if (origsize < sz)
	subOp = op;
      else if (extra != 0)
	extraOp = op;
      else
	addOp = op;
    }
  }
  spacebase_vn = newConstant(sz,0);
  spacebase_vn->updateType(sb_type,true,true);
  spacebase_vn->setFlags(Varnode::spacebase);
  if (addOp == (PcodeOp *)0) {
    addOp = newOp(2,op->getAddr());
    opSetOpcode(addOp,CPUI_PTRSUB);
    newUniqueOut(sz,addOp);
    opInsertBefore(addOp,op);
  }
  else {
    opSetOpcode(addOp,CPUI_PTRSUB);
  }
  outvn = addOp->getOut();
  // Make sure newconstant and extra preserve origval in address units
  uintb newconstoff = origval - extra;		// everything is already in address units
  newconst = newConstant(sz,newconstoff);
  newconst->setSymbolCheck(Varnode::symcheck_complete);	// No longer need to check this constant as symbolref
  if (spaceid->isTruncated())
    addOp->setPtrFlow();
  opSetInput(addOp,spacebase_vn,0);
  opSetInput(addOp,newconst,1);

  Symbol *sym = entry->getSymbol();
  Datatype *entrytype = sym->getType();
  Datatype *ptrentrytype = glb->types->getTypePointerStripArray(sz,entrytype,spaceid->getWordSize());
  bool typelock = sym->isTypeLocked();
  if (typelock && (entrytype->getMetatype() == TYPE_UNKNOWN))
    typelock = false;
  outvn->updateType(ptrentrytype,typelock,false);
  if (extra != 0) {
    if (extraOp == (PcodeOp *)0) {
      extraOp = newOp(2,op->getAddr());
      opSetOpcode(extraOp,CPUI_INT_ADD);
      newUniqueOut(sz,extraOp);
      opInsertBefore(extraOp,op);
    }
    else
      opSetOpcode(extraOp,CPUI_INT_ADD);
    Varnode *extconst = newConstant(sz,extra);
    extconst->setSymbolCheck(Varnode::symcheck_complete);
    opSetInput(extraOp,outvn,0);
    opSetInput(extraOp,extconst,1);
    outvn = extraOp->getOut();
  }
  if (sz < origsize) {		// The new constant is smaller than the original varnode, so we extend it
    if (zextOp == (PcodeOp *)0) {
      zextOp = newOp(1,op->getAddr());
      opSetOpcode(zextOp,CPUI_INT_ZEXT); // Create an extension to get back to original varnode size
      newUniqueOut(origsize,zextOp);
      opInsertBefore(zextOp,op);
    }
    else
      opSetOpcode(zextOp,CPUI_INT_ZEXT);
    opSetInput(zextOp,outvn,0);
    outvn = zextOp->getOut();
  }
  else if (origsize < sz) {	// The new constant is bigger than the original varnode, truncate it
    if (subOp == (PcodeOp *)0) {
      subOp = newOp(2,op->getAddr());
      opSetOpcode(subOp,CPUI_SUBPIECE);
      newUniqueOut(origsize,subOp);
      opInsertBefore(subOp,op);
    }
    else
      opSetOpcode(subOp,CPUI_SUBPIECE);
    opSetInput(subOp,outvn,0);
    opSetInput(subOp,newConstant(4, 0), 1);	// Take least significant piece
    outvn = subOp->getOut();
  }
  if (!isCopy)
    opSetInput(op,outvn,slot);
}

void Funcdata::clearCallSpecs(void)

{
  int4 i;

  for(i=0;i<qlst.size();++i)
    delete qlst[i];		// Delete each func_callspec

  qlst.clear();			// Delete list of pointers
}

void Funcdata::issueDatatypeWarning(Datatype *dt)

{
  string warn = glb->types->findWarning(dt);
  if (warn.size() > 0)
    warningHeader(warn);
}

FuncCallSpecs *Funcdata::getCallSpecs(const PcodeOp *op) const

{
  int4 i;
  const Varnode *vn;

  vn = op->getIn(0);
  if (vn->getSpace()->getType()==IPTR_FSPEC)
    return FuncCallSpecs::getFspecFromConst(vn->getAddr());

  for(i=0;i<qlst.size();++i)
    if (qlst[i]->getOp() == op) return qlst[i];
  return (FuncCallSpecs *)0;
}

/// \brief Compare call specification objects by call site address
///
/// \param a is the first call specification to compare
/// \param b is the second call specification
/// \return \b true if the first call specification should come before the second
bool Funcdata::compareCallspecs(const FuncCallSpecs *a,const FuncCallSpecs *b)

{
  int4 ind1,ind2;
  ind1 = a->getOp()->getParent()->getIndex();
  ind2 = b->getOp()->getParent()->getIndex();
  if (ind1 != ind2) return (ind1 < ind2);
  return (a->getOp()->getSeqNum().getOrder() < b->getOp()->getSeqNum().getOrder());
}

/// Calls are put in dominance order so that earlier calls get evaluated first.
/// Order affects parameter analysis.
void Funcdata::sortCallSpecs(void)

{
  sort(qlst.begin(),qlst.end(),compareCallspecs);
}

/// This is used internally if a CALL is removed (because it is unreachable)
/// \param op is the particular specification to remove
void Funcdata::deleteCallSpecs(PcodeOp *op)

{
  vector<FuncCallSpecs *>::iterator iter;

  for(iter=qlst.begin();iter!=qlst.end();++iter) {
    FuncCallSpecs *fc = *iter;
    if (fc->getOp() == op) {
      delete fc;
      qlst.erase(iter);
      return;
    }
  }
}

/// If \e extrapop is unknown, recover it from what we know about this function
/// and set the value permanently for \b this Funcdata object.
/// If there is no function body it may be impossible to know the value, in which case
/// this returns the reserved value indicating \e extrapop is unknown.
///
/// \return the recovered value
int4 Funcdata::fillinExtrapop(void)

{
  if (hasNoCode())		// If no code to make a decision on
    return funcp.getExtraPop();	// either we already know it or we don't

  if (funcp.getExtraPop() != ProtoModel::extrapop_unknown)
    return funcp.getExtraPop();	// If we already know it, just return it

  list<PcodeOp *>::const_iterator iter = beginOp(CPUI_RETURN);
  if (iter == endOp(CPUI_RETURN)) return 0; // If no return statements, answer is irrelevant
  
  PcodeOp *retop = *iter;
  uint1 buffer[4];

  glb->loader->loadFill(buffer,4,retop->getAddr());

  // We are assuming x86 code here
  int4 extrapop = 4;		// The default case
  if (buffer[0] == 0xc2) {
    extrapop = buffer[2];	// Pull out immediate 16-bits
    extrapop <<= 8;
    extrapop += buffer[1];
    extrapop += 4;		// extra 4 for the return address
  }
  funcp.setExtraPop( extrapop ); // Save what we have learned on the prototype
  return extrapop;
  
}

/// A description of each Varnode currently involved in the data-flow of \b this
/// function is printed to the output stream.  This is suitable as part of a console mode
/// or debug view of the function at any point during its analysis
/// \param s is the output stream
void Funcdata::printVarnodeTree(ostream &s) const

{
  VarnodeDefSet::const_iterator iter,enditer;
  Varnode *vn;

  iter = vbank.beginDef();
  enditer = vbank.endDef();
  while(iter != enditer) {
    vn = *iter++;
    vn->printInfo(s);
  }
}

/// Each scope has a set of memory ranges associated with it, encompassing
/// storage locations of variables that are \e assumed to be in the scope.
/// Each range for each local scope is printed.
/// \param s is the output stream
void Funcdata::printLocalRange(ostream &s) const

{
  localmap->printBounds(s);
  ScopeMap::const_iterator iter,enditer;
  iter = localmap->childrenBegin();
  enditer = localmap->childrenEnd();
  for(;iter!=enditer;++iter) {
    Scope *l1 = (*iter).second;
    l1->printBounds(s);
  }
}

/// Parse a \<jumptablelist> element and build a JumpTable object for
/// each \<jumptable> child element.
/// \param decoder is the stream decoder
void Funcdata::decodeJumpTable(Decoder &decoder)

{
  uint4 elemId = decoder.openElement(ELEM_JUMPTABLELIST);
  while(decoder.peekElement() != 0) {
    unique_ptr<JumpTable> jt(new JumpTable());
    jt->decode(decoder);
    jumpvec.push_back(jt.release());
  }
  decoder.closeElement(elemId);
}

/// A \<jumptablelist> element is written with \<jumptable> children describing
/// each jump-table associated with the control-flow of \b this function.
/// \param encoder is the stream encoder
void Funcdata::encodeJumpTable(Encoder &encoder) const

{
  if (jumpvec.empty()) return;
  vector<JumpTable *>::const_iterator iter;

  encoder.openElement(ELEM_JUMPTABLELIST);
  for(iter=jumpvec.begin();iter!=jumpvec.end();++iter)
    (*iter)->encode(encoder);
  encoder.closeElement(ELEM_JUMPTABLELIST);
}

/// \brief Encode descriptions for a set of Varnodes to a stream
///
/// This is an internal function for the function's marshaling system.
/// Individual elements are written in sequence for Varnodes in a given set.
/// The set is bounded by iterators using the 'loc' ordering.
/// \param encoder is the stream encoder
/// \param iter is the beginning of the set
/// \param enditer is the end of the set
void Funcdata::encodeVarnode(Encoder &encoder,VarnodeLocSet::const_iterator iter,VarnodeLocSet::const_iterator enditer)

{
  Varnode *vn;
  while(iter!=enditer) {
    vn = *iter++;
    vn->encode(encoder);
  }
}

/// This produces a single \<highlist> element, with a \<high> child for each
/// high-level variable (HighVariable) currently associated with \b this function.
/// \param encoder is the stream encoder
void Funcdata::encodeHigh(Encoder &encoder) const

{
  Varnode *vn;
  HighVariable *high;

  if (!isHighOn()) return;
  encoder.openElement(ELEM_HIGHLIST);
  VarnodeLocSet::const_iterator iter;
  for(iter=beginLoc();iter!=endLoc();++iter) {
    vn = *iter;
    if (vn->isAnnotation()) continue;
    high = vn->getHigh();
    if (high->isMark()) continue;
    high->setMark();
    high->encode(encoder);
  }
  for(iter=beginLoc();iter!=endLoc();++iter) {
    vn = *iter;
    if (!vn->isAnnotation())
      vn->getHigh()->clearMark();
  }
  encoder.closeElement(ELEM_HIGHLIST);
}

/// A single \<ast> element is produced with children describing Varnodes, PcodeOps, and
/// basic blocks making up \b this function's current syntax tree.
/// \param encoder is the stream encoder
void Funcdata::encodeTree(Encoder &encoder) const

{
  encoder.openElement(ELEM_AST);
  encoder.openElement(ELEM_VARNODES);
  for(int4 i=0;i<glb->numSpaces();++i) {
    AddrSpace *base = glb->getSpace(i);
    if (base == (AddrSpace *)0 || base->getType()==IPTR_IOP) continue;
    VarnodeLocSet::const_iterator iter = vbank.beginLoc(base);
    VarnodeLocSet::const_iterator enditer = vbank.endLoc(base);
    encodeVarnode(encoder,iter,enditer);
  }
  encoder.closeElement(ELEM_VARNODES);
  
  list<PcodeOp *>::iterator oiter,endoiter;
  PcodeOp *op;
  BlockBasic *bs;
  for(int4 i=0;i<bblocks.getSize();++i) {
    bs = (BlockBasic *)bblocks.getBlock(i);
    encoder.openElement(ELEM_BLOCK);
    encoder.writeSignedInteger(ATTRIB_INDEX, bs->getIndex());
    bs->encodeBody(encoder);
    oiter = bs->beginOp();
    endoiter = bs->endOp();
    while(oiter != endoiter) {
      op = *oiter++;
      op->encode(encoder);
    }
    encoder.closeElement(ELEM_BLOCK);
  }
  for(int4 i=0;i<bblocks.getSize();++i) {
    bs = (BlockBasic *)bblocks.getBlock(i);
    if (bs->sizeIn() == 0) continue;
    encoder.openElement(ELEM_BLOCKEDGE);
    encoder.writeSignedInteger(ATTRIB_INDEX, bs->getIndex());
    bs->encodeEdges(encoder);
    encoder.closeElement(ELEM_BLOCKEDGE);
  }
  encoder.closeElement(ELEM_AST);
}

/// A description of \b this function is written to the stream,
/// including name, address, prototype, symbol, jump-table, and override information.
/// If indicated by the caller, a description of the entire PcodeOp and Varnode
/// tree is also emitted.
/// \param encoder is the stream encoder
/// \param id is the unique id associated with the function symbol
/// \param saveTree is \b true if the p-code tree should be emitted
/// \param saveOverrides is \b true if information about overrides should be emitted
void Funcdata::encode(Encoder &encoder,uint8 id,bool saveTree,bool saveOverrides) const

{
  encoder.openElement(ELEM_FUNCTION);
  if (id != 0)
    encoder.writeUnsignedInteger(ATTRIB_ID, id);
  encoder.writeString(ATTRIB_NAME, name);
  encoder.writeSignedInteger(ATTRIB_SIZE, size);
  if (hasNoCode())
    encoder.writeBool(ATTRIB_NOCODE, true);
  baseaddr.encode(encoder);

  if (!hasNoCode()) {
    localmap->encodeRecursive(encoder,false);	// Save scope and all subscopes
  }

  if (saveTree) {
    encodeTree(encoder);
    encodeHigh(encoder);
  }
  encodeJumpTable(encoder);
  funcp.encode(encoder);		// Must be saved after database
  if (saveOverrides)
    localoverride.encode(encoder,glb);
  encoder.closeElement(ELEM_FUNCTION);
}

/// Parse a \<function> element, recovering the name, address, prototype, symbol,
/// jump-table, and override information for \b this function.
/// \param decoder is the stream decoder
/// \return the symbol id associated with the function
uint8 Funcdata::decode(Decoder &decoder)

{
  //  clear();  // Shouldn't be needed
  name.clear();
  size = -1;
  uint8 id = 0;
  AddrSpace *stackid = glb->getStackSpace();
  uint4 elemId = decoder.openElement(ELEM_FUNCTION);
  for(;;) {
    uint4 attribId = decoder.getNextAttributeId();
    if (attribId == 0) break;
    if (attribId == ATTRIB_NAME)
      name = decoder.readString();
    else if (attribId == ATTRIB_SIZE) {
      size = decoder.readSignedInteger();
    }
    else if (attribId == ATTRIB_ID) {
      id = decoder.readUnsignedInteger();
    }
    else if (attribId == ATTRIB_NOCODE) {
      if (decoder.readBool())
	flags |= no_code;
    }
    else if (attribId == ATTRIB_LABEL)
      displayName = decoder.readString();
  }
  if (name.size() == 0)
    throw LowlevelError("Missing function name");
  if (displayName.size() == 0)
    displayName = name;
  if (size == -1)
    throw LowlevelError("Missing function size");
  baseaddr = Address::decode( decoder );
  for(;;) {
    uint4 subId = decoder.peekElement();
    if (subId == 0) break;
    if (subId == ELEM_LOCALDB) {
      if (localmap != (ScopeLocal *)0)
	throw LowlevelError("Pre-existing local scope when restoring: "+name);
      ScopeLocal *newMap = new ScopeLocal(id,stackid,this,glb);
      glb->symboltab->decodeScope(decoder,newMap);	// May delete newMap and throw
      localmap = newMap;
    }
    else if (subId == ELEM_OVERRIDE)
      localoverride.decode(decoder,glb);
    else if (subId == ELEM_PROTOTYPE) {
      if (localmap == (ScopeLocal *)0) {
	// If we haven't seen a <localdb> tag yet, assume we have a default local scope
	ScopeLocal *newMap = new ScopeLocal(id,stackid,this,glb);
	Scope *scope = glb->symboltab->getGlobalScope();
	glb->symboltab->attachScope(newMap,scope);	// May delete newMap and throw
	localmap = newMap;
      }
      funcp.setScope(localmap,baseaddr+ -1); // localmap built earlier
      funcp.decode(decoder,glb);
    }
    else if (subId == ELEM_JUMPTABLELIST)
      decodeJumpTable(decoder);
  }
  decoder.closeElement(elemId);
  if (localmap == (ScopeLocal *)0) { // Seen neither <localdb> or <prototype>
    // This is a function shell, so we provide default locals
    ScopeLocal *newMap = new ScopeLocal(id,stackid,this,glb);
    Scope *scope = glb->symboltab->getGlobalScope();
    glb->symboltab->attachScope(newMap,scope);		// May delete newMap and throw
    localmap = newMap;
    funcp.setScope(localmap,baseaddr+ -1);
  }
  localmap->resetLocalWindow();
  return id;
}

/// \brief Inject p-code from a \e payload into \b this live function
///
/// Raw PcodeOps are generated from the payload within a given basic block at a specific
/// position in \b this function.
/// \param payload is the injection payload
/// \param addr is the address at the point of injection
/// \param bl is the given basic block holding the new ops
/// \param iter indicates the point of insertion
void Funcdata::doLiveInject(InjectPayload *payload,const Address &addr,BlockBasic *bl,list<PcodeOp *>::iterator iter)

{
  PcodeEmitFd emitter;
  InjectContext &context(glb->pcodeinjectlib->getCachedContext());

  emitter.setFuncdata(this);
  context.clear();
  context.baseaddr = addr;		// Shouldn't be using inst_next, inst_next2 or inst_start here
  context.nextaddr = addr;

  list<PcodeOp *>::const_iterator deaditer = obank.endDead();
  bool deadempty = (obank.beginDead() == deaditer);
  if (!deadempty)
    --deaditer;
  payload->inject(context,emitter);
  // Calculate iterator to first injected op
  if (deadempty)
    deaditer = obank.beginDead();
  else
    ++deaditer;
  while(deaditer != obank.endDead()) {
    PcodeOp *op = *deaditer;
    ++deaditer;
    if (op->isCallOrBranch())
      throw LowlevelError("Illegal branching injection");
    opInsert(op,bl,iter);
  }
}

void PcodeEmitFd::dump(const Address &addr,OpCode opc,VarnodeData *outvar,VarnodeData *vars,int4 isize)

{				// Convert template data into a real PcodeOp
  PcodeOp *op;
  Varnode *vn;

  if (outvar != (VarnodeData *)0) {
    Address oaddr(outvar->space,outvar->offset);
    op = fd->newOp(isize,addr);
    fd->newVarnodeOut(outvar->size,oaddr,op);
  }
  else
    op = fd->newOp(isize,addr);
  fd->opSetOpcode(op,opc);
  int4 i=0;
  if (op->isCodeRef()) { // Is the first input parameter a code reference
    Address addrcode(vars[0].space,vars[0].offset);
    // addrcode.toPhysical()  // For backward compatibility with SLED
    fd->opSetInput(op,fd->newCodeRef(addrcode),0);
    i += 1;
    // This is handled by FlowInfo
    //    if ((opc==CPUI_CALL)&&(addrcode==pos->getNaddr())) {
      // This is probably PIC code and the call is really a jump
    //      fd->op_setopcode(op,CPUI_BRANCH);
    //    }
  }
  for(;i<isize;++i) {
    vn = fd->newVarnode(vars[i].size,vars[i].space,vars[i].offset);
    fd->opSetInput(op,vn,i);
  }
}

/// \brief Get the resolution (union field) associated with an unresolved data-type and the given edge
///
/// If there is no field associated with the edge, or the resolved data-type is not
/// the same size as the given data-type, null is returned.
/// \param unresType is the given data-type being resolved
/// \param op is the PcodeOp component of the given edge
/// \param slot is the slot component of the given edge
/// \return the associated field as a ResolvedUnion or null
const ResolvedUnion *Funcdata::getUnionField(const Datatype *unresType,const PcodeOp *op,int4 slot) const

{
  map<ResolveEdge,ResolvedUnion>::const_iterator iter;
  ResolveEdge edge(unresType,op,slot);
  iter = unionMap.find(edge);
  if (iter != unionMap.end()) {
    const ResolvedUnion *res = &(*iter).second;
    Datatype *dt = res->getDatatype();
    // Check that the Varnode size matches the data-type
    if (unresType->getSize() == dt->getSize())
      return res;
  }
  return (const ResolvedUnion *)0;
}

/// \brief Get the resolution (union field) associated with an unresolved data-type and the given edge
///
/// The size of the resolved data-type does not have to match the given data-type.
/// If there is no field associated with the edge, null is returned.
/// \param unresType is the unresolved data-type
/// \param op is the PcodeOp component of the given edge
/// \param slot is the slot component of the given edge
/// \return the associated field as a ResolvedUnion or null
const ResolvedUnion *Funcdata::getUnionResolution(const Datatype *unresType,const PcodeOp *op,int4 slot) const

{
  map<ResolveEdge,ResolvedUnion>::const_iterator iter;
  ResolveEdge edge(unresType,op,slot);
  iter = unionMap.find(edge);
  if (iter != unionMap.end()) {
    const ResolvedUnion *res = &(*iter).second;
    return res;
  }
  return (const ResolvedUnion *)0;
}

/// \brief Get any resolution (union field) associated with an unresolved data-type and the given address
///
/// If there is no associated field, null is returned
/// \param unresType is the unresolved data-type
/// \param addr is the given address
/// \param slot is the slot component the resolution matches
/// \return the associated field as a ResolvedUnion or null
const ResolvedUnion *Funcdata::getAddressBasedUnionField(const Datatype *unresType,const Address &addr,int4 slot) const

{
  map<ResolveEdge,ResolvedUnion>::const_iterator iter;
  ResolveEdge edge(unresType,addr,slot);
  iter = unionMap.find(edge);
  if (iter != unionMap.end())
    return &(*iter).second;
  return (const ResolvedUnion *)0;
}

/// \brief Associate a resolution (union field) with an unresolved data-type and the given edge
///
/// If there was a previous association, it is overwritten unless it was \e locked.
/// The method returns \b true except in this case where a previous locked association exists.
/// \param unresType is the unresolved data-type
/// \param op is the PcodeOp component of the given edge
/// \param slot is the slot component of the given edge
/// \param resolve is the resolved union
/// \return \b true unless the resolution was locked
bool Funcdata::setUnionField(const Datatype *unresType,const PcodeOp *op,int4 slot,const ResolvedUnion &resolve)

{
  ResolveEdge edge(unresType,op,slot);
  pair<map<ResolveEdge,ResolvedUnion>::iterator,bool> res;
  res = unionMap.emplace(edge,resolve);
  if (!res.second) {
    if (!(*res.first).second.update(resolve))
      return !(*res.first).second.isLocked();
  }
  if (op->code() == CPUI_MULTIEQUAL && slot >= 0) {
    // Data-type propagation doesn't happen between MULTIEQUAL input slots holding the same Varnode
    // So if this is a MULTIEQUAL, copy resolution to any other input slots holding the same Varnode
    const Varnode *vn = op->getIn(slot);		// The Varnode being directly set
    for(int4 i=0;i<op->numInput();++i) {
      if (i == slot) continue;
      if (op->getIn(i) != vn) continue;		// Check that different input slot holds same Varnode
      ResolveEdge dupedge(unresType,op,i);
      res = unionMap.emplace(dupedge,resolve);
      if (!res.second) {
	(*res.first).second.update(resolve);
      }
    }
  }
  return true;
}

/// \brief Associate a resolution (union field) with an unresolved data-type and the given address
///
/// If there was a previous association, it is overwritten unless it was \e locked.
/// The method returns \b true except in this case where a previous locked association exists.
/// \param unresType is the unresolved data-type
/// \param addr is the given address
/// \param slot is the slot component the resolution matches
/// \param resolve is the resolved union
/// \return \b true unless there was a locked association
bool Funcdata::setAddressBasedUnionField(const Datatype *unresType,const Address &addr,int4 slot,const ResolvedUnion &resolve)

{
  ResolveEdge edge(unresType,addr,slot);
  pair<map<ResolveEdge,ResolvedUnion>::iterator,bool> res;
  res = unionMap.emplace(edge,resolve);
  if (!res.second) {
    if ((*res.first).second.isLocked()) {
      return false;
    }
    (*res.first).second = resolve;
  }
  return true;
}

/// \brief Update the resolution data-type associated with an unresolved data-type for a given edge
///
/// If further resolution of the cached data-type has happened, update the cache with the new data-type.
/// \param unresType is the unresolved data-type
/// \param op is the PcodeOp component of the given edge
/// \param slot is the slot component of the given edge
/// \param resType is the new resolution data-type
/// \return \b true unless there was a locked association
bool Funcdata::updateUnionField(const Datatype *unresType,const PcodeOp *op,int4 slot,Datatype *resType)

{
  map<ResolveEdge,ResolvedUnion>::iterator iter;
  ResolveEdge edge(unresType,op,slot);
  iter = unionMap.find(edge);
  if (iter != unionMap.end()) {
    (*iter).second.setResolve(resType);
    return true;
  }
  return false;
}

/// \brief Force a specific resolution (union field) resolution for an unresolved data-type and the given edge
///
/// \param unresType is the unresolved data-type
/// \param fieldNum is the index of the field to force
/// \param op is PcodeOp of the edge
/// \param slot is -1 for the write edge or >=0 indicating the particular read edge
void Funcdata::forceFacingType(Datatype *unresType,int4 fieldNum,PcodeOp *op,int4 slot)

{
  Datatype *baseType = unresType;
  if (baseType->getMetatype() == TYPE_PTR)
    baseType = ((TypePointer *)baseType)->getPtrTo();
  if (unresType->isPointerRel()) {
    // Don't associate a relative pointer with the resolution, but convert to a standard pointer
    unresType = glb->types->getTypePointer(unresType->getSize(), baseType, ((TypePointer *)unresType)->getWordSize());
  }
  ResolvedUnion resolve(unresType,fieldNum,*glb->types);
  setUnionField(unresType, op, slot, resolve);
}

/// \brief Copy a read/write facing resolution for an unresolved data-type from one PcodeOp to another
///
/// \param unresType is the unresolved data-type
/// \param op is the new reading PcodeOp
/// \param slot is the new slot (-1 for write, >=0 for read)
/// \param oldOp is the PcodeOp to inherit the resolution from
/// \param oldSlot is the old slot (-1 for write, >=0 for read)
/// \return the field number that was inherited
int4 Funcdata::inheritUnionField(Datatype *unresType,const PcodeOp *op,int4 slot,PcodeOp *oldOp,int4 oldSlot)

{
  map<ResolveEdge,ResolvedUnion>::const_iterator iter;
  if (slot < 0 && oldOp->isMarker())
    slot = 0;
  ResolveEdge edge(unresType,oldOp,oldSlot);
  iter = unionMap.find(edge);
  if (iter == unionMap.end())
    return -1;
  setUnionField(unresType,op,slot,(*iter).second);
  return (*iter).second.getFieldNum();
}

/// \brief Create a resolution for an unresolved pointer data-type based on earlier non-pointer resolution
///
/// \param unresPtr is the unresolved pointer data-type
/// \param op is the new reading PcodeOp
/// \param slot is the new slot (-1 for write, >=0 for read)
/// \param oldOp is the PcodeOp to inherit the resolution from
/// \param oldSlot is the old slot (-1 for write, >=0 for read)
/// \return the field number that was inherited
int4 Funcdata::inheritUnionFieldPtr(Datatype *unresPtr,const PcodeOp *op,int4 slot,PcodeOp *oldOp,int4 oldSlot)

{
  Datatype *parent = unresPtr->getDepend(0);
  if (slot < 0 && oldOp->isMarker())
    slot = 0;
  map<ResolveEdge,ResolvedUnion>::const_iterator iter;
  ResolveEdge edge(parent,oldOp,oldSlot);
  iter = unionMap.find(edge);
  if (iter == unionMap.end())
    return -1;
  ResolvedUnion ptrres(unresPtr,(*iter).second.getFieldNum(),*glb->types);
  setUnionField(unresPtr,op,slot,ptrres);
  return (*iter).second.getFieldNum();
}

#ifdef OPACTION_DEBUG

/// The current state of the op is recorded for later comparison after
/// its been modified.
/// \param op is the given PcodeOp being recorded
void Funcdata::debugModCheck(PcodeOp *op)

{
  if (op->isModified()) return;
  if (!debugCheckRange(op)) return;
  op->setAdditionalFlag(PcodeOp::modified);
  ostringstream before;
  op->printDebug(before);
  modify_list.push_back(op);
  modify_before.push_back( before.str() );
}

void Funcdata::debugModClear(void)

{
  for(int4 i=0;i<modify_list.size();++i)
    modify_list[i]->clearAdditionalFlag(PcodeOp::modified);
  modify_list.clear();
  modify_before.clear();
  opactdbg_active = false;
}

/// \param actionname is the name of the Action being debugged
void Funcdata::debugModPrint(const string &actionname)

{
  if (!opactdbg_active) return;
  opactdbg_active = false;
  if (modify_list.empty()) return;
  PcodeOp *op;
  ostringstream s;
  opactdbg_breakon |= (opactdbg_count == opactdbg_breakcount);

  s << "DEBUG " << dec << opactdbg_count++ << ": " << actionname << endl;
  for(int4 i=0;i<modify_list.size();++i) {
    op = modify_list[i];
    s << modify_before[i] << endl;
    s << "   ";
    op->printDebug(s);
    s << endl;
    op->clearAdditionalFlag(PcodeOp::modified);
  }
  modify_list.clear();
  modify_before.clear();
  glb->printDebug(s.str());
}

/// \param pclow is the beginning of the memory range to trace
/// \param pchigh is the end of the range
/// \param uqlow is an (optional) sequence number to associate with the beginning of the range
/// \param uqhigh is an (optional) sequence number to associate with the end of the range
void Funcdata::debugSetRange(const Address &pclow,const Address &pchigh,
				      uintm uqlow,uintm uqhigh)

{
  opactdbg_on = true;
  opactdbg_pclow.push_back(pclow);
  opactdbg_pchigh.push_back(pchigh);
  opactdbg_uqlow.push_back(uqlow);
  opactdbg_uqhigh.push_back(uqhigh);
}

/// \param op is the given PcodeOp to check
/// \return \b true if the op is being traced
bool Funcdata::debugCheckRange(PcodeOp *op)

{
  int4 i,size;

  size = opactdbg_pclow.size();
  for(i=0;i<size;++i) {
    if (!opactdbg_pclow[i].isInvalid()) {
      if (op->getAddr() < opactdbg_pclow[i])
	continue;
      if (opactdbg_pchigh[i] < op->getAddr())
	continue;
    }
    if (opactdbg_uqlow[i] != ~((uintm)0)) {
      if (opactdbg_uqlow[i] > op->getTime())
	continue;
      if (opactdbg_uqhigh[i] < op->getTime())
	continue;
    }
    return true;
  }
  return false;
}

void Funcdata::debugPrintRange(int4 i) const

{
  ostringstream s;
  if (!opactdbg_pclow[i].isInvalid()) {
    s << "PC = (";
    opactdbg_pclow[i].printRaw(s);
    s << ',';
    opactdbg_pchigh[i].printRaw(s);
    s << ")  ";
  }
  else
    s << "entire function ";
  if (opactdbg_uqlow[i] != ~((uintm)0)) {
    s << "unique = (" << hex << opactdbg_uqlow[i] << ',';
    s << opactdbg_uqhigh[i] << ')';
  }
  glb->printDebug(s.str());
}

#endif

} // End namespace ghidra
