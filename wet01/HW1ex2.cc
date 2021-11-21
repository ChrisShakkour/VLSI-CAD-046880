#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"
#include <queue>
#include <bits/stdc++.h>

using namespace std;

bool verbose = false;

#define INPUT 1
#define DONE  10
#define UNDONE 99
#define VISITED 35
#define UNVISITED 44
#define ZERO 0

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
  

  /*direct output to file*/
  string fileName = cellName + string(".rank");
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
  
  fv << "file name: " << fileName << endl;
  
  /* enter your code here */

  hcmCell *flatCell = hcmFlatten(cellName + string("_flat"), topCell, globalNodes);
  cout << "-I- Top cell flattened" << endl;

  std::vector< std::pair<int, std::string> > database;
  std::queue<hcmNode*> nodeQueue;

  /* initiating Nodes property */ 
  std::map< std::string, hcmNode*>::const_iterator pI;
  for(pI = flatCell->getNodes().begin();
      pI != flatCell->getNodes().end(); pI++){
        hcmNode* node = (*pI).second;
        node->setProp("done", UNDONE);
        node->setProp("level", ZERO);
  string nNode = node->getName();
  if(nNode == "VSS" || nNode == "VDD"){
    nodeQueue.push(node);
    cout << "#### VSS VDD" <<endl;
    }      
  }

 
 

  // push input nodes of flat cell to queue.
  vector<hcmPort*> ports = flatCell->getPorts();
  vector<hcmPort*>::iterator iP;
  for(iP = ports.begin(); iP != ports.end(); iP++){
    hcmPort* port = (*iP);
  hcmNode* node = port->owner();
        if(port->getDirection()==INPUT){
          nodeQueue.push(node);
  }
  } 


  //setting all Instports unvisited;
  //setting all instances to level0;
  std::map< std::string, hcmInstance*>::const_iterator jI;
  for(jI = flatCell->getInstances().begin();
      jI != flatCell->getInstances().end(); jI++){
  hcmInstance* inst = (*jI).second;
  inst->setProp("level", ZERO);
  inst->setProp("done", UNDONE);
  std::map< std::string, hcmInstPort*>::const_iterator ipI;
        for( ipI = inst->getInstPorts().begin();
             ipI != inst->getInstPorts().end(); ipI++){
                hcmInstPort* instPort = (*ipI).second;
                instPort->setProp("visited", UNVISITED);
  }
  }


  while(!nodeQueue.empty()){
  hcmNode* node = nodeQueue.front();
  nodeQueue.pop();
    cout << "-> Handeling Node : " << node->getName() <<endl;
  node->setProp("done", DONE);
  std::queue<hcmInstance*> instQueue; 
  std::map< std::string, hcmInstPort*>::const_iterator ipI;
        for( ipI = node->getInstPorts().begin();
             ipI != node->getInstPorts().end(); ipI++){
          hcmInstPort* instPort = (*ipI).second;
    instPort->setProp("visited", VISITED);
    hcmInstance* inst = instPort->getInst();
    int idone;
    inst->getProp("done", idone);
    if(idone == UNDONE){
      instQueue.push(inst);
                  cout << "found instance : " << inst->getName();
                  cout << " connected to cell : " << inst->masterCell()->getName() <<endl;
    } 
    }
  
  while(!instQueue.empty()){
    hcmInstance* inst = instQueue.front();
          instQueue.pop();
    //int stat;
    //inst->getProp("done", stat);
    cout << "handeling instance : " << inst->getName() <<endl;
    int Done=DONE;
    /* check all port inputs */
    std::queue<hcmInstPort*> oQueue;
    std::map< std::string, hcmInstPort*>::const_iterator ipI;
          for( ipI = inst->getInstPorts().begin();
                   ipI != inst->getInstPorts().end(); ipI++){
      hcmInstPort* instPort = (*ipI).second;
      hcmPort* port = instPort->getPort();
      cout << "found instPort : " << instPort->getName()  <<endl;
      int stat;
                        instPort->getProp("visited", stat);
                        cout << "Visisted : " << stat <<endl;
      if(port->getDirection()==INPUT){
        if(stat != VISITED) Done = UNDONE;
      }
      else oQueue.push(instPort);
    }
    
    int nodeLevel;
    node->getProp("level", nodeLevel);
    
    if(Done==DONE){ 
      cout << "----------DONE----------" <<endl;  
      while(!oQueue.empty()){
        hcmInstPort* instPort = oQueue.front();
        oQueue.pop();
        hcmNode* newNode = instPort->getNode();
        int x;
        newNode->getProp("done", x);
        if(x==UNDONE) {
          nodeQueue.push(newNode);
          newNode->setProp("level", nodeLevel + 1);
          cout << "Node Level : " << nodeLevel<<endl;
        }
      } 
      inst->setProp("level", nodeLevel);
      inst->setProp("done", DONE);    
      //fv << (nodeLevel);
      //fv << " " <<inst->getName() <<endl;
      database.push_back( make_pair(nodeLevel,inst->getName()));
      //database.insert(std::pair<int, std::string>(nodeLevel, inst->getName()));
    }       
  } 
  }


 std::vector< std::pair<int, std::string> >::const_iterator iI;
 sort(database.begin(), database.end());
 for( iI = database.begin(); iI != database.end(); iI++){
    //cout << (*iI).first << " " << (*iI).second <<endl;
    fv << (*iI).first;
    fv << " " << (*iI).second <<endl;
 }


 return(0);
}





