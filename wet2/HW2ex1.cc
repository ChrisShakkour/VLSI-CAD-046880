#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"
#include "hcmvcd.h"
#include <iostream>
#include <set>
#include <string>
#include <stdlib.h>
#include "hcmsigvec.h"
#include <queue>


using namespace std;

bool verbose = false;

///////////////////////////////////////////////////////////////////////////

// Event Class, used to contain Node pointer and its value
class Event{
  public:
    hcmNode *Node;
    bool val;
    Event(hcmNode *N,bool value){
      Node=N;
      val=value;
    }

} ;

/* functions declarations */
bool IsGlobalNode(hcmNode *Node,set< string> &globalNodes);
void Simulate_Gate(hcmInstance * inst,std::queue< Event > &EventQueue);
void Gate_Processor(std::queue< Event > &EventQueue,std::queue<hcmInstance *> &GateQueue);
bool Gate_Exist(std::queue<hcmInstance *> GateQueue,string inst_name);
void Event_Processor(std::queue< Event > &EventQueue,std::queue<hcmInstance *> &GateQueue);
// implementation in the end 

int main(int argc, char **argv) {
  int argIdx = 1;
  int anyErr = 0;
  unsigned int i;
  vector<string> vlgFiles;
  string sigsFileName;
  string vecsFileName;

  if (argc < 5) {
    anyErr++;
  } else {
    if (!strcmp(argv[argIdx], "-v")) {
      argIdx++;
      verbose = true;
    }
    for (int i=argIdx;i < argc; i++) {
      vlgFiles.push_back(string(argv[i]));
    }
    sigsFileName = string(argv[1+argIdx++]);
    vecsFileName = string(argv[1+argIdx++]);
    
    if (vlgFiles.size() < 4) {
      cerr << "-E- At least top-level, signals-file, vectors-file and single verilog file required for spec model" << endl;
      anyErr++;
    }
  }

  if (anyErr) {
    cerr << "Usage: " << argv[0] << "  [-v] top-cell sigFile vecFile file1.v [file2.v] ... \n";
    exit(1);
  }
 
  set< string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");

  hcmDesign* design = new hcmDesign("design");
  string cellName = vlgFiles[0];
  for (i = 3; i < vlgFiles.size(); i++) {
    printf("-I- Parsing verilog %s ...\n", vlgFiles[i].c_str());
    if (!design->parseStructuralVerilog(vlgFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << vlgFiles[i] << " aborting." << endl;
      exit(1);
    }
  }
  
  hcmCell *topCell = design->getCell(cellName);
  if (!topCell) {
    printf("-E- could not find cell %s\n", cellName.c_str());
    exit(1);
  }
  
  hcmCell *flatCell = hcmFlatten(cellName + string("_flat"), topCell, globalNodes);
  cout << "-I- Top cell flattened" << endl;

  hcmSigVec parser(sigsFileName, vecsFileName, verbose);
  if(!parser.good()){
    exit(1);
  }
  set<string> sigs;
  parser.getSignals(sigs);
  for (set<string>::iterator I= sigs.begin(); I != sigs.end(); I++) {
    cout << "SIG: " << (*I) << endl;
  }

  // For any nodes set property: first_run initialized as true. so that even if output 
  // doesn't change in the first run, we add it to the EventQueue anyway.
  // set propety for Nodes : cur_val,prev_val Initalized with false.
  // if it is a Global Node of type VDD cur_val=true.
  std::map< std::string, hcmNode* >::const_iterator nI;
  for (nI =flatCell->getNodes().begin(); nI != flatCell->getNodes().end(); nI++){
    hcmNode *node= nI->second;
    node->setProp("cur_val",false);
    // node->setProp("prev_val",false);  
    node->setProp("first_run",true);    
    if(node->getName()=="VDD"){
      node->setProp("cur_val",true); 
      // node->setProp("prev_val",true); 
    }
    if(node->getName()=="CLK"){ 
      node->setProp("prev_CLK",false); 
    }
  } 

  // set property for dff instance: prev_val = false. it stores the data from the previous cycle.
  map<string,hcmInstance*>::const_iterator iI;
  for (iI=flatCell->getInstances().begin(); iI!=flatCell->getInstances().end();iI++) {
      string instname = iI->first; 
      hcmInstance* inst = iI->second;   
      if (string::npos != instname.find("dff")) {
        inst->setProp("prev_val", false);   
      }
  }
  // Event queue containing Events Class (hcmNode *Node,bool new_value)
  std::queue< Event > EventQueue;
  // Gate queue containing instances (hcmInstance *)
  std::queue< hcmInstance * > GateQueue;


  // vcd file initalization
  vcdFormatter vcd(cellName + ".vcd", flatCell, globalNodes);
  if (!vcd.good()) {
    printf("-E- Could not create vcdFormatter for cell: %s\n", cellName.c_str());
    exit(1);
  }
  //controlling simulation time
  unsigned int time = 0;
  //no instance parents of in the flat model.
  list<const hcmInstance*> noInsts;

  // read the vectors file one line at a time until the eof
  // cout << "-I- Reading vectors ... " << endl;
  while (parser.readVector() == 0) {
    vcd.changeTime(time);
    // cout << "$Time = " << time <<endl;
    for (set<string>::iterator I= sigs.begin(); I != sigs.end(); I++) {
      string name = (*I);
      bool val;
      parser.getSigValue(name, val);
      hcmNode *node=flatCell->getNode(name);
      Event new_event(node,val);
      EventQueue.push(new_event);
      if(node->getName()=="CLK"){
        bool prev_clk;
        node->getProp("cur_val",prev_clk); 
        node->setProp("prev_CLK",prev_clk); 
      }
      // cout << "  " << name << " = " << (val? "1" : "0")  << endl;
    }
    while(!EventQueue.empty()){
      Event_Processor(EventQueue,GateQueue);
      if(!GateQueue.empty()){
        Gate_Processor(EventQueue,GateQueue);
      }
    }
    // printing Intermediate values
    for (nI =flatCell->getNodes().begin(); nI != flatCell->getNodes().end(); nI++){
      hcmNode *node= nI->second;
      if(IsGlobalNode(node,globalNodes)){
        continue;
      }
      string node_name=node->getName();
      bool newVal;
      node->getProp("cur_val",newVal);
      // cout << node_name<<" = "<<newVal <<endl;
      hcmNodeCtx *nodeCtx = new hcmNodeCtx(noInsts,node);
      if (nodeCtx) {
        vcd.changeValue(nodeCtx, newVal);
        delete nodeCtx;
      }
    }
    time++; 
    // cout << "-I- Reading next vectors ... " << endl;
  }

  return(0);
}
 

/* functions implementation*/

// Gate processor function, reponsible of simulation of instances from GateQueue 
// and follow that, adding new events to EventQueue
void Gate_Processor(std::queue< Event > &EventQueue,std::queue<hcmInstance *> &GateQueue){
  while(!GateQueue.empty()){
    hcmInstance *inst=GateQueue.front();
    // simulating the instance, and updating EventQueue accordingly
    Simulate_Gate(inst,EventQueue);
    //removing the instance from GateQueue
    GateQueue.pop();
  }
}


// simulate OR gate ,return the result , and changes output_node accordingly
bool logic_OR(hcmInstance *inst,hcmNode **output_node){
  bool result=false;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      *output_node=node;
    }
    if(ip->getPort()->getDirection()==IN){
      bool temp_res;
      if(node->getProp("cur_val",temp_res)==OK){
        result=result || temp_res;
      }
    }
  }
  return result;
}

// simulate XOR gate ,return the result , and changes output_node accordingly
bool logic_XOR(hcmInstance *inst,hcmNode **output_node){
  bool result=false;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      *output_node=node;
    }
    if(ip->getPort()->getDirection()==IN){
      bool temp_res;
      if(node->getProp("cur_val",temp_res)==OK){
        result=result ^ temp_res;
      }
    }
  }
  return result;
}

// simulate AND gate ,return the result , and changes output_node accordingly
bool logic_AND(hcmInstance *inst,hcmNode **output_node){
  bool result=true;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      *output_node=node;
    }
    if(ip->getPort()->getDirection()==IN){
      bool temp_res;
      if(node->getProp("cur_val",temp_res)==OK){
        result=result && temp_res;
      }
    }
  }
  return result;
}

// simulate BUFFER gate , return the result , and changes output_node accordingly
bool logic_BUFFER(hcmInstance *inst,hcmNode **output_node){
  bool result=false;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      *output_node=node;
    }
    if(ip->getPort()->getDirection()==IN){
      bool temp_res;
      if(node->getProp("cur_val",temp_res)==OK){
        result=temp_res;
      }
    }
  }
  return result;
}

// simulate DFF gate , return the result of output , and changes output_node accordingly
// in addition it return CLK=true as an argument iff on rising edge.
bool DFF(hcmInstance *inst,hcmNode **output_node,bool &CLK){
  bool cur_clk=false,prev_clk=false;
  bool data=false,cur_result=false;
  // cout<< "######" << inst->getName() <<endl;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      *output_node=node;
      node->getProp("cur_val",cur_result);
    }
    if(ip->getPort()->getDirection()==IN){
      bool temp_res;
      if(ip->getPort()->getName()=="CLK"){
        node->getProp("cur_val",cur_clk);
        node->getProp("prev_CLK",prev_clk);
        // cout<< "node "<<node->getName()<<" = "<< cur_clk  <<endl;
      } else{
        node->getProp("cur_val",data);
        // node->getProp("prev_val",data);
        // cout<< "(prev result) node "<<node->getName()<<" = "<< data  <<endl;
      }
    }
  }
  CLK=false;
  if(cur_clk==true && prev_clk==false){
    bool prev_data;
    inst->getProp("prev_val", prev_data);
    CLK=true;
    return prev_data; 
  }
  inst->setProp("prev_val", data);
  return cur_result;
}

// function responsible of simulating instance and incrementing EventQueue accordingly
void Simulate_Gate(hcmInstance * inst,std::queue< Event > &EventQueue){
  string logic_name= inst->masterCell()->getName();
  hcmNode* output_node;
  bool result, inverted=false,IsDFF=false,CLK=false;
  if(logic_name.find("nor")!=std::string::npos){
    result=!(logic_OR(inst,&output_node));
    inverted=true;
  } else if(logic_name.find("xor")!=std::string::npos){
    result=logic_XOR(inst,&output_node);
  } else if(logic_name.find("or")!=std::string::npos){
    result=logic_OR(inst,&output_node);
  } else if(logic_name.find("nand")!=std::string::npos){
    result=!(logic_AND(inst,&output_node));
    inverted=true;
  } else if(logic_name.find("and")!=std::string::npos){
    result=logic_AND(inst,&output_node);
  } else if(logic_name.find("buffer")!=std::string::npos){
    result=logic_BUFFER(inst,&output_node);
  } else if(logic_name.find("inv")!=std::string::npos){
    result=!(logic_BUFFER(inst,&output_node));
    inverted=true;
  } else if(logic_name.find("not")!=std::string::npos){
    result=!(logic_BUFFER(inst,&output_node));
    inverted=true;
  } else if(logic_name.find("dff")!=std::string::npos){
    result=DFF(inst,&output_node,CLK);
    IsDFF=true;
  } else{
    cerr << "-E- does not support gate type: " << logic_name << " aborting." << endl;
    exit(1);
  }
  //Applying the result to the output node
  bool prev_val ;
  bool first_run=false;
  output_node->getProp("cur_val",prev_val);
  output_node->getProp("first_run",first_run);
  if(first_run){
    if(IsDFF&&CLK){
      output_node->setProp("first_run",false);
      first_run=true;
    }
    else{
      first_run=false;
      if((result==0)&&inverted){
        first_run=true;
      }
    }
  }

  // if output changes we push new event to EventQueue (Event-Driven approach)
  if( prev_val!=result || first_run){
    Event new_event(output_node,result);
    EventQueue.push(new_event);
  }
}

// Event processor function, reponsible of events from EventQueue 
// and follow that, adding new gates to GateQueue
void Event_Processor(std::queue< Event > &EventQueue,std::queue<hcmInstance *> &GateQueue){
  while(!EventQueue.empty()){
    Event E=EventQueue.front();
    hcmNode *node=E.Node;
    bool new_val = E.val;

    // copying new value to the Event's node 
    bool temp=false;
    node->setProp("cur_val",new_val);

    // Iterating on Instances in the fanout of the node and pushing them to GateQueue
    std::map<std::string, hcmInstPort* >::const_iterator ipI;
    for (ipI =node->getInstPorts().begin(); ipI != node->getInstPorts().end(); ipI++){
      hcmInstPort *ip= ipI->second;
      hcmInstance *inst=ip->getInst();
      if(ip->getPort()->getDirection()==IN){
        if(!Gate_Exist(GateQueue,inst->getName())){
          GateQueue.push(inst);
        }
      }
    }
    //removing E from EventQueue
    EventQueue.pop();
  }

}

/*
  return true if instance with name inst_name exist in the GateQueue.
  note: calling GateQueue by value so that Copy C'tor is called.
*/
bool Gate_Exist(std::queue<hcmInstance *> GateQueue,string inst_name){
  while(!GateQueue.empty()){
    hcmInstance * inst=GateQueue.front();
    GateQueue.pop();
    if(inst->getName()==inst_name) return true;
  }
  return false;
}

/*
returns true if Node is a global Node
*/
bool IsGlobalNode(hcmNode *Node,set< string> &globalNodes){
  string nodeName=Node->getName();
  return (globalNodes.find(nodeName) != globalNodes.end());
}
