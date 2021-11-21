#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"

#include <algorithm> //for the sort
using namespace std;

bool verbose = false;

///////////////////////////////////////////////////////////////////////////

/* functions declarations */
int max_depth_Node(hcmNode *topNode,set< string> &globalNodes);
void Search_AND(hcmCell *topCell,int &AND_count);
void deepest_nodes_names(hcmCell *topCell,vector<string> &deepest_names,set< string> &globalNodes);
void max_depth_hierarchy(hcmCell *topCell,int cur_depth,int &cur_max);
void deepest_nodes_names_aux(hcmCell *topCell,string cur_name,int depth_to_max,vector<string> &deepest_names,set< string> &globalNodes);
bool IsGlobalNode(hcmNode *Node,set< string> &globalNodes);
// implementation in the end 

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

  /* section a */
  int num_instances = topCell->getInstances().size(); // number of instances in the map of topCell.Instances
  fv << "a. Number of instances in the top-level cell: "<< num_instances << endl;

  /* section b */
  int num_nodes = 0;
  std::map< std::string, hcmNode*>::const_iterator hnI;
  for(hnI = topCell->getNodes().begin();
      hnI != topCell->getNodes().end(); hnI++){
    if(!IsGlobalNode(hnI->second,globalNodes)){
      num_nodes++;
    }
  }
  fv << "b. Number of nodes in the top-level cell: "<< num_nodes << endl;

  /* section c */
  int depth=0,max_depth=1;
  std::map< std::string, hcmNode* >::const_iterator nI;
  for (nI =topCell->getNodes().begin(); nI != topCell->getNodes().end(); nI++){
    hcmNode *node= nI->second;
    depth=max_depth_Node(node,globalNodes);
    /* if it is a global node it return -1, so it can't be the deepest anyway,
     so checking it again is useless */
    if(depth>max_depth){
      max_depth=depth;
    }
  } 
  fv << "c. Levels of hierarchy with the deepest reach: "<< max_depth << endl;

  /* section d */  
  int AND_gates =0;
  Search_AND(topCell,AND_gates);
  fv << "d. Instances of the cell 'and' in the Folded model: "<< AND_gates << endl;

  /* section e */  
  int NAND_gates =0;
  std::map< std::string, hcmInstance* >::const_iterator iI;
  for (iI =flatCell->getInstances().begin(); iI != flatCell->getInstances().end(); iI++){
    hcmInstance *inst=iI->second;
    string inst_name =inst->masterCell()->getName();
    if(!inst_name.compare("nand")){
      NAND_gates++;
    }
  }
  fv << "e. Instances of the cell 'nand' in the entire hierarchy: "<< NAND_gates << endl;

  /* section f */
  vector<string> deepest_names;
  deepest_nodes_names(topCell,deepest_names,globalNodes);
  //sorting lexicographically
  std::sort(deepest_names.begin(),deepest_names.end());

  std::vector<string>::iterator sI = deepest_names.begin();
  for(;sI!=deepest_names.end();sI++){
    fv << *sI << endl;
  }
  return(0);
}
 

/* functions implementation*/

//returns true if Node is a global Node
bool IsGlobalNode(hcmNode *Node,set< string> &globalNodes){
  string nodeName=Node->getName();
  return (globalNodes.find(nodeName) != globalNodes.end());
}

// for section c
/*
  Return the depth of the deepest path from a source Node to te lowest level (used for section c).
  */
int max_depth_Node(hcmNode *topNode,set< string> &globalNodes){
  if(IsGlobalNode(topNode,globalNodes)){
    return -1; // returning -1 means it is a global node
  }

  int depth=0,max_depth=0;
  std::map< std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =topNode->getInstPorts().begin(); ipI != topNode->getInstPorts().end(); ipI++){
    hcmInstPort *inst_port= ipI->second;
    hcmNode* node=inst_port->getPort()->owner(); //the node connecting the instPort from inside
    depth = max_depth_Node(node,globalNodes);
    if(depth==-1){
      continue;
    }
    if(depth>max_depth){
      max_depth=depth;
    }
  } 
  return max_depth+1;
}

// for section d
/*
  A function which scans the Folded model and counts the number of 
  AND gates without repetition (all instances of the same Cell counts once).
  Idea: set 'visited' property to a cell we've already visited. 
*/
void Search_AND(hcmCell *topCell,int &AND_count){
  int visited=0;
  topCell->getProp("Visited",visited);
  if(visited==1){
    return;
  }

  topCell->setProp("Visited",1);

  std::map< std::string, hcmInstance* >::const_iterator iI;
  for (iI =topCell->getInstances().begin(); iI != topCell->getInstances().end(); iI++){
    hcmInstance *inst= iI->second;
    hcmCell* cell=inst->masterCell(); //the master of the instance
    if(!cell->getName().compare("and")){ //if we look at instance of type AND
      AND_count++;
    }
    else{  
      Search_AND(cell,AND_count);  
    }
  }
  return;
}

// for section f
/*
  A function which call all the auxiliary functions with the proper arguements.
  Could be written inside main instead, I chose to write it for clarity.
*/
void deepest_nodes_names(hcmCell *topCell,vector<string> &deepest_names,set< string> &globalNodes){
  int max_depth=1;
  max_depth_hierarchy(topCell,1,max_depth);
  deepest_nodes_names_aux(topCell,"",max_depth-1,deepest_names,globalNodes);
}

/*
  Auxiliary function to find the the deepest hierarchies (used for section f) 
  and update deepest_names according to the nodes paths.
  topCell - the cell from where we begin.
  cur_name - the current path name (update each hierarchy we get in).
  depth_to_max - how mant more hierarchies to get inside
  (it is max_depth minus 1).
  deepest_names - vector of strings to save the deepest nodes names.
  globalNodes - the global nodes names.
*/
void deepest_nodes_names_aux(hcmCell *topCell,string cur_name,int depth_to_max,vector<string> &deepest_names,set< string> &globalNodes){
  if(depth_to_max==0){ //means we are at maximum depth, we will add the cell's nodes to the vector names
    std::map< std::string, hcmNode* >::const_iterator nI;
    for (nI =topCell->getNodes().begin(); nI != topCell->getNodes().end(); nI++){
      hcmNode *node= nI->second;
      if(!IsGlobalNode(node,globalNodes)){
        deepest_names.push_back(cur_name+node->getName());
      }
    } 
    return;
  }
  // If it isn't the deepest hirearchy, continue more inside
  std::map< std::string, hcmInstance* >::const_iterator iI;
  for (iI =topCell->getInstances().begin(); iI != topCell->getInstances().end(); iI++){
    hcmInstance *inst= iI->second;
    hcmCell* cell=inst->masterCell(); //the master of the instance

    string instance_name= inst->getName();
    deepest_nodes_names_aux(cell,cur_name+instance_name+"/",depth_to_max-1,deepest_names,globalNodes); 
  } 

  return;
}
/*
  Auxiliary function to find the depth of the deepest hierarchies (used for section f) and update cur_max accordingly.
  cur_max - current max depth, should be updated if cur_depth is bigger, initialized by 1
  (not that it is passed 'by reference' beacuse we would like to update it
  during the recursive calls).
  cur_depth - depth of the current hierarchy, should be incremented recursively, initialized by 1 by definition
  (cur_depth passed 'by value' so that copy c'tor is called, and recursive changes does
  not affect earlier calls).
*/
void max_depth_hierarchy(hcmCell *topCell,int cur_depth,int &cur_max){
  if(topCell->getInstances().size()){
  // If it isn't the deepest hirearchy, continue inside
    std::map< std::string, hcmInstance* >::const_iterator iI;
    for (iI =topCell->getInstances().begin(); iI != topCell->getInstances().end(); iI++){
      hcmInstance *inst= iI->second;
      hcmCell* cell=inst->masterCell(); //the master of the instance
      max_depth_hierarchy(cell,cur_depth+1,cur_max);
    }  
  }
  else{
    // we arrived to a primitive cell
    if(cur_depth>cur_max){
      cur_max=cur_depth;
    }
  }
  return;
}
