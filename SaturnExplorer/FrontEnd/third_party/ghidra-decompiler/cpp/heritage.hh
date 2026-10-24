/* ###
 * IP: GHIDRA
 * NOTE: Phi placement and renaming based on ACM journal articles
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
 * Modified for SaturnExplorer: a guard below a pointer stepped down is no array's start (patches/0056-engine-stack-walk-down-aliases.patch).
 * Modified for SaturnExplorer: an escape analysis of the frame addresses, failing closed (patches/0061-engine-stack-frame-escape.patch).
 * Modified for SaturnExplorer: once the frame escapes, a pointer not proved to be no frame address may be one (patches/0063-engine-frame-escape-unknown-pointers.patch).
 * Modified for SaturnExplorer: the escape's lowest frame offset never rises over the queries of a function (patches/0065-engine-frame-escape-whole-frame.patch).
 * Modified for SaturnExplorer: a frame address that is not an exact offset from the frame base escapes, unless a loop only steps it up (patches/0066-engine-frame-escape-inexact.patch).
 */
/// \file heritage.hh
/// \brief Utilities for building Static Single Assignment (SSA) form 

#ifndef __HERITAGE_HH__
#define __HERITAGE_HH__

#include "block.hh"

namespace ghidra {

/// Container holding the stack system for the renaming algorithm.  Every disjoint address
/// range (indexed by its initial address) maps to its own Varnode stack.
typedef map<Address,vector<Varnode *> > VariableStack;

/// \brief Map object for keeping track of which address ranges have been heritaged
///
/// We keep track of a fairly fine grained description of when each address range
/// was entered in SSA form, referred to as \b heritaged or, for Varnode objects,
/// no longer \b free.  An address range is added using the add() method, which includes
/// the particular pass when it was entered.  The map can be queried using findPass()
/// that informs the caller whether the address has been heritaged and if so in which pass.
class LocationMap {
public:
  /// \brief Label for describing extent of address range that has been heritaged
  struct SizePass {
    int4 size;			///< Size of the range (in bytes)
    int4 pass;			///< Pass when the range was heritaged
  };
  /// Iterator into the main map
  typedef map<Address,SizePass>::iterator iterator;
private:
  map<Address,SizePass> themap;	///< Heritaged addresses mapped to range size and pass number
public:
  iterator add(Address addr,int4 size,int4 pass,int4 &intersect); ///< Mark new address as \b heritaged
  iterator find(const Address &addr);			///< Look up if/how given address was heritaged
  int4 findPass(const Address &addr) const;		///< Look up if/how given address was heritaged
  iterator erase(iterator iter) { return themap.erase(iter); }	///< Remove a particular entry from the map
  iterator begin(void) { return themap.begin(); }	///< Get starting iterator over heritaged ranges
  iterator end(void) { return themap.end(); }		///< Get ending iterator over heritaged ranges
  void clear(void) { themap.clear(); }			///< Clear the map of heritaged ranges
};

/// \brief An address range to be processed
struct MemRange {
  /// Properties of a single address range
  enum {
    new_addresses = 1,	///< The range covers addresses that have not been seen in previous passes
    old_addresses = 2	///< The range covers addresses that were seen in previous passes
  };
  Address addr;		///< Starting address of the range
  int4 size;		///< Number of bytes in the range
  uint4 flags;		///< Property flags
  MemRange(const Address &ad,int4 sz,uint4 fl) : addr(ad) { size = sz; flags = fl; }	///< Constructor
  bool newAddresses(void) const { return ((flags & new_addresses)!=0); }	///< Does \b this range cover new addresses?
  bool oldAddresses(void) const { return ((flags & old_addresses)!=0); }	///< Does \b this range cover old addresses?
  void clearProperty(uint4 val) { flags &= ~val; }	///< Clear specific properties from the memory range
};

/// \brief A list of address ranges that need to be converted to SSA form
///
/// The disjoint list of ranges are built up and processed in a single pass.  The container is fed a list
/// of ranges that may be overlapping but are already in address order. It constructs a disjoint list by taking
/// the union of overlapping ranges.
class TaskList {
public:
  /// Iterator in the list
  typedef list<MemRange>::iterator iterator;
private:
  list<MemRange> tasklist;		///< List of address ranges that needs to be processed
public:
  void add(Address addr,int4 size,uint4 fl);				///< Add a range to the list
  iterator insert(iterator pos,Address addr,int4 size,uint4 fl);	///< Insert a disjoint range in the list
  iterator erase(iterator iter) { return tasklist.erase(iter); }	///< Remove a particular range
  iterator begin(void) { return tasklist.begin(); }			///< Get iterator to beginning of \b this list
  iterator end(void) { return tasklist.end(); }				///< Get iterator to end of \b this list
  void clear(void) { tasklist.clear(); }				///< Clear all ranges in the list
  bool empty(void) { return tasklist.empty(); }				///< Return \b true if the list is empty
};

/// \brief Priority queue for the phi-node (MULTIEQUAL) placement algorithm
///
/// A \e work-list for basic blocks used during phi-node placement.  Implemented as
/// a set of stacks with an associated priority.  Blocks are placed in the \e queue
/// with an associated \e priority (or depth) using the insert() method.  The current
/// highest priority block is retrieved with the extract() method.
class PriorityQueue {
  vector<vector<FlowBlock *> > queue; ///< An array of \e stacks, indexed by priority
  int4 curdepth;		      ///< The current highest priority index with active blocks
public:
  PriorityQueue(void) { curdepth = -2; } ///< Constructor
  void reset(int4 maxdepth);		 ///< Reset to an empty queue
  void insert(FlowBlock *bl,int4 depth); ///< Insert a block into the queue given its priority
  FlowBlock *extract(void);		 ///< Retrieve the highest priority block
  bool empty(void) const { return (curdepth==-1); } ///< Return \b true if \b this queue is empty
};

class Funcdata;
class FuncCallSpecs;

/// \brief Information about heritage passes performed for a specific address space
///
/// For a particular address space, this keeps track of:
///   - how long to delay heritage
///   - how long to delay dead code removal
///   - whether dead code has been removed (for this space)
///   - have warnings been issued
class HeritageInfo {
  friend class Heritage;
  AddrSpace *space;		///< The address space \b this record describes
  int4 delay;			///< How many passes to delay heritage of this space
  int4 deadcodedelay;		///< How many passes to delay deadcode removal of this space
  int4 deadremoved;		///< >0 if Varnodes in this space have been eliminated
  bool loadGuardSearch;		///< \b true if the search for LOAD ops to guard has been performed
  bool warningissued;		///< \b true if warning issued previously
  bool hasCallPlaceholders;	///< \b true for the \e stack space, if stack placeholders have not been removed
  bool isHeritaged(void) const { return (space != (AddrSpace *)0); }	///< Return \b true if heritage is performed on this space
  void reset(void);		///< Reset the state
public:
  HeritageInfo(AddrSpace *spc);	///< Constructor
};

/// \brief Description of a LOAD operation that needs to be guarded
///
/// Heritage maintains a list of CPUI_LOAD ops that reference the stack dynamically. These
/// can potentially alias stack Varnodes, so we maintain what (possibly limited) information
/// we known about the range of stack addresses that can be referenced.
class LoadGuard {
  friend class Heritage;
  PcodeOp *op;		///< The LOAD op
  AddrSpace *spc;	///< The stack space being loaded from
  uintb pointerBase;	///< Base offset of the pointer
  uintb minimumOffset;	///< Minimum offset of the LOAD
  uintb maximumOffset;	///< Maximum offset of the LOAD
  int4 step;		///< Step of any access into this range (0=unknown)
  int4 analysisState;	///< 0=unanalyzed, 1=analyzed(partial result), 2=analyzed(full result)
  bool guessedBelow;	///< SaturnExplorer: minimumOffset is a guess below a pointer stepped down, not where an array starts (patches/0056-engine-stack-walk-down-aliases.patch)
  void establishRange(const ValueSetRead &valueSet);	///< Convert partial value set analysis into guard range
  static bool pointerSteppedDown(Varnode *vn);	///< SaturnExplorer (0065): is the pointer the join of a loop that steps it down
  void finalizeRange(const ValueSetRead &valueSet);	///< Convert value set analysis to final guard range

  /// \brief Set a new unanalyzed LOAD guard that initially guards everything
  ///
  /// \param o is the LOAD op
  /// \param s is the (stack) space it is loading from
  /// \param off is the base offset that is indexed from
  void set(PcodeOp *o,AddrSpace *s,uintb off) {
    op = o; spc = s; pointerBase=off; minimumOffset=0; maximumOffset=s->getHighest(); step=0; analysisState=0; guessedBelow=false;
  }
public:
  PcodeOp *getOp(void) const { return op; }	///< Get the PcodeOp being guarded
  uintb getMinimum(void) const { return minimumOffset; }	///< Get minimum offset of the guarded range
  uintb getMaximum(void) const { return maximumOffset; }	///< Get maximum offset of the guarded range
  int4 getStep(void) const { return step; }		///< Get the calculated step associated with the range (or 0)
  bool isGuessedBelow(void) const { return guessedBelow; }	///< SaturnExplorer: is the minimum a guess below a pointer stepped down
  bool isGuarded(const Address &addr) const;		///< Does \b this guard apply to the given address
  bool isRangeLocked(void) const { return (analysisState == 2); }	///< Return \b true if the range is fully determined
  bool isValid(OpCode opc) const { return (!op->isDead() && op->code() == opc); }	///< Return \b true if the record still describes an active LOAD
};

/// \brief SaturnExplorer (patches/0061-engine-stack-frame-escape.patch): the escape analysis of a function's frame addresses
///
/// The stack analysis (AliasChecker, discoverIndexedStackPointers, and the walks of patches 0056, 0057 and 0060) knows a pointer into the
/// frame only through a list of exact forms, and every other form failed open: a frame address laundered through an op it does not model
/// (a mask, a shift or swap pair, halves reassembled), stored to memory and read back, joined with another pointer, or returned by a call it
/// was handed to was taken for no frame address, so the stores through it were no stores to the stack and the locals below it were folded
/// (ledger A3-85, A3-88, A3-86 b). This walk fails closed instead. A frame address \e escapes when it, or a value computed from it by any
/// op, (a) is an input to an op other than a copy, an addition, a subtraction of a constant, a join, an INDIRECT, a PTRSUB or a PTRADD (an
/// address operand of a LOAD or STORE is an access, a comparison's one-bit output cannot carry an address, a return leaves the frame);
/// (b) is stored to memory; (c) is joined with a value that is no frame address; (d) is returned by a call it was handed to, and reaches a
/// memory access from there. Where the escaped value goes is not followed: once one escapes, every local from the lowest frame offset up
/// are aliased, and every STORE through a value that may be one is a store to the stack.
///
/// SaturnExplorer (patches/0063-engine-frame-escape-unknown-pointers.patch): the escape of 0061 followed the escaped value through the
/// function's own ops, and every way a frame address came back that was no op of the function -- a callee that keeps it (in a global, in a
/// slot of the frame it was given a pointer to, in a register the model keeps), a call's output in another register -- was found by a
/// reviewer one form at a time (ledger A3-89, A3-91). It now uses the escape semantics of a C compiler's points-to analysis instead of a
/// list: (e) a frame address handed to a call as an argument (a register or stack input, not the stack pointer itself) escapes, and so does
/// the frame when the host declares an escape the p-code does not show (ScopeLocal::declareHostEscape). Once anything escaped, every
/// pointer whose origin the analysis cannot prove is no frame address -- a value loaded from memory, any output of a call or a user op,
/// the value a call or a store may have rewritten (an INDIRECT), a parameter, and anything computed from them -- may point into the frame,
/// and every STORE through one is a STORE through an unmodeled frame address (guarded). Where the escaped value goes is still not followed:
/// that is what makes the rule closed.
///
/// SaturnExplorer (patches/0066-engine-frame-escape-inexact.patch): the walk down below a frame address was recognised by its form since
/// 0056 (a constant step, a conditional step, a spill, an index, a register stride), and each list failed open on the next form (ledger
/// A3-65, A3-72, A3-80, A3-116, A3-118, A3-119). Now a frame address the stack analysis cannot place exactly is an escape as well: one whose
/// value is not the frame base plus a constant (a register or an index added or subtracted, a join of two different offsets, a value loaded
/// or computed by any op the walk does not model), or one a loop carries, unless every step of the loop adds a constant that is not
/// negative (then it is at or above the loop's lowest entry, which is aliased). An access through such an address, or a call it is handed
/// to, makes the frame escape, with the whole-frame alias of 0065, and the access's guard covers the whole stack space. The forms of 0056
/// to 0065 stay only as helpers that add aliasing.
class FrameEscape {
  void markUnknownStores(const Funcdata *fd);	///< Mark the STOREs through a pointer not proved to be no frame address (0063)
  void classifyExact(AddrSpace *spc,const map<Varnode *,uintb> &offset,set<Varnode *> &inexactSet);	///< (0066) Which members are no exact offset
public:
  set<Varnode *> members;		///< The possible frame addresses
  set<Varnode *> unmodeled;		///< ...of which those reached through a form the stack analysis does not model
  set<PcodeOp *> unmodeledStores;	///< STOREs through an unmodeled possible frame address, or, once the frame escaped, an unknown pointer
  set<PcodeOp *> inexactLoads;		///< LOADs through a frame address that is no exact offset (0066)
  vector<uintb> upLows;			///< The lowest entry offset of each loop that only steps a frame address up (0066)
  bool inexact;				///< A frame address that is no exact offset reaches an access or a call (0066)
  bool memoryEscape;			///< A frame address was stored to memory
  bool handed;				///< A frame address was handed to a call as an argument (0063)
  bool hostEscape;			///< The host declared an escape the p-code does not show (0063)
  bool escaped;				///< Some frame address escaped
  bool loose;				///< ...by a form the stack analysis does not model (0061's causes, not a hand-over alone: 0063)
  bool gaveUp;				///< The walk passed its bound: everything is taken to have escaped
  bool haveLowest;			///< \b lowest is set
  uintb lowest;				///< The lowest frame offset reached (a byte offset in the stack space), or any earlier query of the function reached (0065)
  FrameEscape(void) : inexact(false), memoryEscape(false), handed(false), hostEscape(false), escaped(false), loose(false), gaveUp(false), haveLowest(false), lowest(0) {}
  void compute(const Funcdata *fd,AddrSpace *spc,bool preHeritage);	///< Run the walk over the given stack space
  /// \brief Is the given STORE made through a possible frame address the stack analysis does not model?
  bool storeIsUnmodeled(PcodeOp *op) const { return gaveUp || unmodeledStores.find(op) != unmodeledStores.end(); }
  /// \brief Is the given LOAD made through a frame address that is no exact offset? (0066)
  bool loadIsInexact(PcodeOp *op) const { return gaveUp || inexactLoads.find(op) != inexactLoads.end(); }
};

/// \brief Manage the construction of Static Single Assignment (SSA) form
///
/// With a specific function (Funcdata), this class links the Varnode and
/// PcodeOp objects into the formal data-flow graph structure, SSA form.
/// The full structure can be built over multiple passes. In particular,
/// this allows register data-flow to be analyzed first, and then stack
/// locations can be discovered and promoted to first-class Varnodes in
/// a second pass.
///
/// Varnodes for which it is not known whether they are written to by a
/// PcodeOp are referred to as \b free.  The method heritage() performs
/// a \e single \e pass of constructing SSA form, collecting any \e eligible
/// free Varnodes for the pass and linking them in to the data-flow. A
/// Varnode is considered eligible for a given pass generally based on its
/// address space (see HeritageInfo), which is the main method for delaying
/// linking for stack locations until they are all discovered. In
/// principle a Varnode can be discovered very late and still get linked
/// in on a subsequent pass. Linking causes Varnodes to gain new descendant
/// PcodeOps, which has impact on dead code elimination (see LocationMap).
///
/// The two big aspects of SSA construction are phi-node placement, performed
/// by placeMultiequals(), and the \e renaming algorithm, performed by rename().
/// The various guard* methods are concerned with labeling analyzing
/// data-flow across function calls, STORE, and LOAD operations.
///
/// The phi-node placement algorithm is from (preprint?)
/// "The Static Single Assignment Form and its Computation"
/// by Gianfranco Bilardi and Keshav Pingali, July 22, 1999
///
/// The renaming algorithm taken from
/// "Efficiently computing static single assignment form and the
///  control dependence graph."
/// R. Cytron, J. Ferrante, B. K. Rosen, M. N. Wegman, and F. K. Zadeck
/// ACM Transactions on Programming Languages and Systems,
/// 13(4):451-490, October 1991
class Heritage {
  /// Extra boolean properties on basic blocks for the Augmented Dominator Tree
  enum heritage_flags {
    boundary_node = 1,		///< Augmented Dominator Tree boundary node
    mark_node = 2,		///< Node has already been in queue
    merged_node = 4		///< Node has already been merged
  };

  /// \brief Node for depth-first traversal of stack references
  struct StackNode {
    enum {
      nonconstant_index = 1,
      multiequal = 2
    };
    Varnode *vn;		///< Varnode being traversed
    uintb offset;		///< Offset relative to base
    uint4 traversals;		///< What kind of operations has this pointer accumulated
    list<PcodeOp *>::const_iterator iter;	///< Next PcodeOp to follow

    /// \brief Constructor
    /// \param v is the Varnode being visited
    /// \param o is the current offset from the base pointer
    /// \param trav indicates what configurations were seen along the path to this Varnode
    StackNode(Varnode *v,uintb o,uint4 trav) {
      vn = v;
      offset = o;
      iter = v->beginDescend();
      traversals = trav;
    }
  };

  Funcdata *fd;		        ///< The function \b this is controlling SSA construction 
  LocationMap globaldisjoint;	///< Disjoint cover of every heritaged memory location
  TaskList disjoint;	///< Disjoint cover of memory locations currently being heritaged
  vector<vector<FlowBlock *> > domchild; ///< Parent->child edges in dominator tree
  vector<vector<FlowBlock *> > augment; ///< Augmented edges
  vector<uint4> flags;		///< Block properties for phi-node placement algorithm
  vector<int4> depth;		///< Dominator depth of individual blocks
  int4 maxdepth;		///< Maximum depth of the dominator tree
  int4 pass;			///< Current pass being executed

  PriorityQueue pq;		///< Priority queue for phi-node placement
  vector<FlowBlock *> merge;	///< Calculate merge points (blocks containing phi-nodes)
  vector<HeritageInfo> infolist; ///< Heritage status for individual address spaces
  list<LoadGuard> loadGuard;	///< List of LOAD operations that need to be guarded
  list<LoadGuard> storeGuard;	///< List of STORE operations taking an indexed pointer to the stack
  vector<PcodeOp *> loadCopyOps;	///< List of COPY ops generated by load guards
  void clearInfoList(void);	 ///< Reset heritage status for all address spaces

  /// \brief Get the heritage status for the given address space
  HeritageInfo *getInfo(AddrSpace *spc) { return &(infolist[spc->getIndex()]); }

  /// \brief Get the heritage status for the given address space
  const HeritageInfo *getInfo(AddrSpace *spc) const { return &(infolist[spc->getIndex()]); }

  void clearStackPlaceholders(HeritageInfo *info);	///< Clear remaining stack placeholder LOADs on any call
  void splitJoinLevel(vector<Varnode *> &lastcombo,vector<Varnode *> &nextlev,JoinRecord *joinrec);
  void splitJoinRead(Varnode *vn,JoinRecord *joinrec);
  void splitJoinWrite(Varnode *vn,JoinRecord *joinrec);
  void floatExtensionRead(Varnode *vn,JoinRecord *joinrec);
  void floatExtensionWrite(Varnode *vn,JoinRecord *joinrec);
  void processJoins(void);
  void buildADT(void);		///< Build the augmented dominator tree
  void removeRevisitedMarkers(const vector<Varnode *> &remove,const Address &addr,int4 size);
  int4 collect(MemRange &memrange,vector<Varnode *> &read,vector<Varnode *> &write,vector<Varnode *> &input,vector<Varnode *> &remove) const;
  bool callOpIndirectEffect(const Address &addr,int4 size,PcodeOp *op) const;
  Varnode *normalizeReadSize(Varnode *vn,PcodeOp *op,const Address &addr,int4 size);
  Varnode *normalizeWriteSize(Varnode *vn,const Address &addr,int4 size);
  Varnode *concatPieces(const vector<Varnode *> &vnlist,PcodeOp *insertop,Varnode *finalvn);
  void splitPieces(const vector<Varnode *> &vnlist,PcodeOp *insertop,const Address &addr,int4 size,Varnode *startvn);
  void findAddressForces(vector<PcodeOp *> &copySinks,vector<PcodeOp *> &forces);
  void propagateCopyAway(PcodeOp *op);
  void handleNewLoadCopies(void);
  void analyzeNewLoadGuards(void);
  void generateLoadGuard(StackNode &node,PcodeOp *op,AddrSpace *spc);
  void generateStoreGuard(StackNode &node,PcodeOp *op,AddrSpace *spc);
  bool protectFreeStores(AddrSpace *spc,vector<PcodeOp *> &freeStores);
  bool discoverIndexedStackPointers(AddrSpace *spc,vector<PcodeOp *> &freeStores,bool checkFreeStores);
  void reprocessFreeStores(AddrSpace *spc,vector<PcodeOp *> &freeStores);
  bool markEscapedStores(AddrSpace *spc,vector<PcodeOp *> &freeStores);	///< SaturnExplorer: mark the STOREs a frame address may have escaped to (patches/0061-engine-stack-frame-escape.patch)
  void widenEscapedStoreGuards(void);	///< SaturnExplorer: a STORE guard through an escaped frame address guards the whole stack (patches/0061-engine-stack-frame-escape.patch)
  void guard(const Address &addr,int4 size,bool guardPerformed,
	     vector<Varnode *> &read,vector<Varnode *> &write,vector<Varnode *> &inputvars);
  void guardInput(const Address &addr,int4 size,vector<Varnode *> &input);
  void guardCallOverlappingInput(FuncCallSpecs *fc,const Address &addr,const Address &transAddr,int4 size);
  void guardOutputOverlap(PcodeOp *callOp,const Address &addr,int4 size,const Address &retAddr,int4 retSize,
			  vector<Varnode *> &write);
  bool tryOutputOverlapGuard(FuncCallSpecs *fc,const Address &addr,const Address &transAddr,int4 size,
			     vector<Varnode *> &write);
  bool tryOutputStackGuard(FuncCallSpecs *fc,const Address &addr,const Address &transAddr,int4 size,
			   int4 outputCharacter,vector<Varnode *> &write);
  void guardOutputOverlapStack(PcodeOp *callOp,const Address &addr,int4 size,const Address &retAddr,int4 retSize,vector<Varnode *> &write);
  void guardCalls(uint4 fl,const Address &addr,int4 size,vector<Varnode *> &write);
  void guardStores(const Address &addr,int4 size,vector<Varnode *> &write);
  void guardLoads(uint4 fl,const Address &addr,int4 size,vector<Varnode *> &write);
  void guardReturnsOverlapping(const Address &addr,int4 size);
  void guardReturns(uint4 fl,const Address &addr,int4 size,vector<Varnode *> &write);
  static void buildRefinement(vector<int4> &refine,const Address &addr,const vector<Varnode *> &vnlist);
  void splitByRefinement(Varnode *vn,const Address &addr,const vector<int4> &refine,vector<Varnode *> &split);
  void refineRead(Varnode *vn,const Address &addr,const vector<int4> &refine,vector<Varnode *> &newvn);
  void refineWrite(Varnode *vn,const Address &addr,const vector<int4> &refine,vector<Varnode *> &newvn);
  void refineInput(Varnode *vn,const Address &addr,const vector<int4> &refine,vector<Varnode *> &newvn);
  void remove13Refinement(vector<int4> &refine);
  TaskList::iterator refinement(TaskList::iterator memiter,const vector<Varnode *> &readvars,
				const vector<Varnode *> &writevars,const vector<Varnode *> &inputvars);
  void visitIncr(FlowBlock *qnode,FlowBlock *vnode);
  void calcMultiequals(const vector<Varnode *> &write);
  void renameRecurse(BlockBasic *bl,VariableStack &varstack);
  void bumpDeadcodeDelay(AddrSpace *spc);
  void placeMultiequals(void);
  void rename(void);
public:
  Heritage(Funcdata *data);	///< Constructor

  int4 getPass(void) const { return pass; }	///< Get overall count of heritage passes

  /// \brief Get the pass number when the given address was heritaged
  ///
  /// \param addr is the given address
  /// \return the pass number or -1 if the address has not been heritaged
  int4 heritagePass(const Address &addr) const { return globaldisjoint.findPass(addr); }
  int4 numHeritagePasses(AddrSpace *spc) const;
  void seenDeadCode(AddrSpace *spc); ///< Inform system of dead code removal in given space 
  int4 getDeadCodeDelay(AddrSpace *spc) const; ///< Get pass delay for heritaging the given space
  void setDeadCodeDelay(AddrSpace *spc,int4 delay); ///< Set delay for a specific space
  bool deadRemovalAllowed(AddrSpace *spc) const;    ///< Return \b true if it is \e safe to remove dead code
  bool deadRemovalAllowedSeen(AddrSpace *spc);
  void markRangeHeritaged(const Address &addr,int4 sz);	///< Mark range as heritaged (on previous pass)
  void buildInfoList(void);	                    ///< Initialize information for each space
  void forceRestructure(void) { maxdepth = -1; }    ///< Force regeneration of basic block structures
  void clear(void);				    ///< Reset all analysis of heritage
  void heritage(void);				    ///< Perform one pass of heritage
  const list<LoadGuard> &getLoadGuards(void) const { return loadGuard; }	///< Get list of LOAD ops that are guarded
  const list<LoadGuard> &getStoreGuards(void) const { return storeGuard; }	///< Get list of STORE ops that are guarded
  const LoadGuard *getStoreGuard(PcodeOp *op) const;	///< Get LoadGuard record associated with given PcodeOp
};

} // End namespace ghidra
#endif
