#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"

using namespace std;

bool verbose = false;
#define UNIQUE 55

int  instCounter(hcmCell* cell);
int  nodeCounter(hcmCell* cell);
void getHeirDepth(int depth, int& depthRef, hcmCell* cell);
void getAndFolded(int& cntRef, hcmCell* cell);
int  getNandFlat(hcmCell* flatCell); 

///////////////////////////////////////////////////////////////////////////

int main(int argc, char **argv) {
  int argIdx = 1;
  int anyErr = 0;
  unsigned int i;
  vector<string> vlgFiles;
  
  if (argc < 3) {
    anyErr++;
  } else {
    if (!strcmp(argv[argIdx], "-v")) {
      argIdx++;
      verbose = true;
    }
    for (;argIdx < argc; argIdx++) {
      vlgFiles.push_back(argv[argIdx]);
    }
    
    if (vlgFiles.size() < 2) {
      cerr << "-E- At least top-level and single verilog file required for spec model" << endl;
      anyErr++;
    }
  }

  if (anyErr) {
    cerr << "Usage: " << argv[0] << "  [-v] top-cell file1.v [file2.v] ... \n";
    exit(1);
  }
 
  set< string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");
  
  hcmDesign* design = new hcmDesign("design");
  string cellName = vlgFiles[0];
  for (i = 1; i < vlgFiles.size(); i++) {
    printf("-I- Parsing verilog %s ...\n", vlgFiles[i].c_str());
    if (!design->parseStructuralVerilog(vlgFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << vlgFiles[i] << " aborting." << endl;
      exit(1);
    }
  }
  

  /*direct to file*/
  string fileName = cellName + string(".stat");
  ofstream fv(fileName.c_str());
  if (!fv.good()) {
    cerr << "-E- Could not open file:" << fileName << endl;
    exit(1);
  }

  hcmCell *topCell = design->getCell(cellName);
  if (!topCell) {
    printf("-E- could not find cell %s\n", cellName.c_str());
    exit(1);
  }
  
  hcmCell *flatCell = hcmFlatten(cellName + string("_flat"), topCell, globalNodes);
  cout << "-I- Top cell flattened" << endl;
  
  fv << "file name: " << fileName << endl;

  /* enter your code here */

 
  /* initial variables*/
  int instCount=0;
  int nodeCount=0;
  int heirDepth=0;
  int andCount=0;
  int nandCount=0;

  /* counting instances one at a time */
  instCount = instCounter(topCell); 

  /* counting nodes that are not VSS nor VDD */
  nodeCount = nodeCounter(topCell); 

  /* get heirDepth level */
  getHeirDepth(0, heirDepth, topCell);

  /* get And count */
  getAndFolded(andCount, topCell); 

  /* using flatenner to count nand cells */
  nandCount = getNandFlat(flatCell); 

  /* epilog, dumping data into .stat file*/
  fv << "\na. number of instances exist: "    << instCount <<endl;
  fv << "b. number of nodes exist: "          << nodeCount <<endl;
  fv << "c. deepest heirarchy level: "        << heirDepth <<endl;
  fv << "d. number of and instances exist in folded model: "  << andCount <<endl;
  fv << "e. number of nand instances exist in flat model: " << nandCount <<endl;

 return(0);
}


int instCounter(hcmCell* cell){
  
  int temp=0; 
  std::map< std::string, hcmInstance*>::const_iterator iI;
  for(iI = cell->getInstances().begin();
      iI != cell->getInstances().end(); iI++)
  {
        temp++;
        //cout << "-I- Instance found: " << (*iI).first <<endl;
        //used for debug only
        } 
  return temp;
}


int nodeCounter(hcmCell* cell){

  /* counting nodes that are not VSS nor VDD */
  int temp=0;
  string nodeName;
  std::map< std::string, hcmNode*>::const_iterator nI;
  for(nI = cell->getNodes().begin();
      nI != cell->getNodes().end(); nI++)
  {
        nodeName = (*nI).first;
        if(nodeName != "VDD" && nodeName != "VSS"){
                temp++;
        	//cout << "-I- Node found: " << nodeName <<endl;
        	//used for debug only
        	}
  	}
  return temp;
}


/* DFS recursive algo*/
void getHeirDepth(int depth, int& depthRef, hcmCell* cell){

    depth++;
    std::map< std::string, hcmInstance*>::const_iterator cI;
    for(cI = cell->getInstances().begin();
        cI != cell->getInstances().end(); cI++)
    {
	//cout << "-I- traversing cell: " << (*cI).first <<endl;
        //cout << "-I- Info: " << (*cI).second <<endl;
	getHeirDepth(depth, depthRef, (*cI).second->masterCell());
        }
    // will be reached once no
    // other instances are found
    // after reaching lowest level
    if(depth>depthRef) depthRef=depth;
    }


void getAndFolded(int& cntRef, hcmCell* cell){

  //int temp=0;
  std::map< std::string, hcmInstance*>::const_iterator cI;
  for(cI = cell->getInstances().begin();
      cI != cell->getInstances().end(); cI++)
    {
      string designName=(*cI).second->masterCell()->getName();
      hcmInstance* inst=(*cI).second;
      //cout << "-I- traversing instance: " << (*cI).first <<endl;
      //cout << "-I- Address: "             << inst <<endl;      
      //cout << "-I- Design Cell: "         << designName <<endl;
      if(designName=="and"){
	int unique=0;
	inst->getProp("unique", unique);
	//cout << "property : " << unique <<endl;
	if(unique!=UNIQUE){
	  inst->setProp("unique", UNIQUE);
	  cntRef++;
	}
      }
      getAndFolded(cntRef, inst->masterCell());
    }
}
        


int getNandFlat(hcmCell* flatCell){

  int temp=0;
  std::map< std::string, hcmInstance*>::const_iterator aI;
  for(aI = flatCell->getInstances().begin();
        aI != flatCell->getInstances().end(); aI++)
        {
        string tempCell = (*aI).second->masterCell()->getName();
        if(tempCell == "nand") temp++;
        //cout << "-I- AND Cell found: " << tempCell <<endl;
        //used for debug only
  	}
  return temp;        
}






