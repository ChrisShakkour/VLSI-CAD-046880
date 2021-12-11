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
  string  cl = "CLK";
  cout << "test: " << cl << "00"<< endl;
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

  // For any output nodes set property: first_run initialized as true. so that even if output 
  // doesn't change in the first run, we add it to the EventQueue anyway.
  std::map< std::string, hcmInstance* >::const_iterator iI;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (iI =flatCell->getInstances().begin(); iI != flatCell->getInstances().end(); iI++){
    hcmInstance* inst= iI->second;
    for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
      hcmInstPort* inst_port = ipI->second;
      if(inst_port->getPort()->getDirection()==OUT){
        hcmNode *node= inst_port->getNode();
        node->setProp("first_run",true);
      }
    }
  }

  // set propety for Nodes : cur_val,prev_val Initalized with false.
  // if it is a Global Node of type VDD cur_val=true.
  std::map< std::string, hcmNode* >::const_iterator nI;
  for (nI =flatCell->getNodes().begin(); nI != flatCell->getNodes().end(); nI++){
    hcmNode *node= nI->second;
    node->setProp("cur_val",false);
    node->setProp("prev_val",false);   
    if(node->getName()=="VDD"){
      node->setProp("cur_val",true);  
    }
  } 

  // Event queue containing Events Class (hcmNode *Node,bool new_value)
  std::queue< Event > EventQueue;
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
  cout << "-I- Reading vectors ... " << endl;
  while (parser.readVector() == 0) {
    vcd.changeTime(time);
    for (set<string>::iterator I= sigs.begin(); I != sigs.end(); I++) {
      string name = (*I);
      bool val;
      parser.getSigValue(name, val);
      hcmNode *node=flatCell->getNode(name);
      Event new_event(node,val);
      EventQueue.push(new_event);
      cout << "  " << name << " = " << (val? "1" : "0")  << endl;
    }
    cout <<"test " <<endl;
    while(!EventQueue.empty()){
      Event_Processor(EventQueue,GateQueue);
      if(!GateQueue.empty()){
        Gate_Processor(EventQueue,GateQueue);
      }
    }
    // printing Intermediate values
    cout <<"## Intermediate Values " <<endl;
    for (nI =flatCell->getNodes().begin(); nI != flatCell->getNodes().end(); nI++){
      hcmNode *node= nI->second;
      if(IsGlobalNode(node,globalNodes)){
        continue;
      }
      string node_name=node->getName();
      bool newVal;
      node->getProp("cur_val",newVal);
      cout << node_name<<" = "<<newVal <<endl;
      hcmNodeCtx *nodeCtx = new hcmNodeCtx(noInsts,node);
      if (nodeCtx) {
        vcd.changeValue(nodeCtx, newVal);
        delete nodeCtx;
      }
    }
    time++; 
    cout << "-I- Reading next vectors ... " << endl;
  }

  return(0);
}
 

/* functions implementation*/

void Gate_Processor(std::queue< Event > &EventQueue,std::queue<hcmInstance *> &GateQueue){
  while(!GateQueue.empty()){
    hcmInstance *inst=GateQueue.front();
    // simulating the instance, and updating EventQueue accordingly
    Simulate_Gate(inst,EventQueue);
    //removing the instance from GateQueue
    GateQueue.pop();
  }
}



bool logic_OR(hcmInstance *inst,hcmNode **output_node){
  bool result=false;
  cout<< "######" << inst->getName() <<endl;
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
      cout<< "node "<<node->getName()<<" = "<< temp_res  <<endl;
    }
  }
  return result;
}

bool logic_XOR(hcmInstance *inst,hcmNode **output_node){
  bool result=false;
  cout<< "######" << inst->getName() <<endl;
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
      cout<< "node "<<node->getName()<<" = "<< temp_res  <<endl;
    }
  }
  return result;
}

bool logic_AND(hcmInstance *inst,hcmNode **output_node){
  bool result=true;
  cout<< "######" << inst->getName() <<endl;
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
      cout<< "node "<<node->getName()<<" = "<< temp_res  <<endl;
    }
  }
  cout<< result <<endl;
  return result;
}
bool logic_BUFFER(hcmInstance *inst,hcmNode **output_node){
  bool result=false;
  cout<< "######" << inst->getName() <<endl;
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
      cout<< "node "<<node->getName()<<" = "<< temp_res  <<endl;
    }
  }
  return result;
}


bool DFF(hcmInstance *inst,hcmNode **output_node){
  bool cur_clk=false,prev_clk=false;
  bool data=false,cur_result=false;
  cout<< "######" << inst->getName() <<endl;
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
        node->getProp("prev_val",prev_clk);
        cout<< "node "<<node->getName()<<" = "<< cur_clk  <<endl;
      } else{
        //node->getProp("cur_val",data);
        node->getProp("prev_val",data);
        cout<< "(prev result) node "<<node->getName()<<" = "<< data  <<endl;
      }
    }
  }

  if(cur_clk==true && prev_clk==false){
    cout<< "rising clock" <<endl;
    return data; 
  }
  return cur_result;
}

void Simulate_Gate(hcmInstance * inst,std::queue< Event > &EventQueue){
  string logic_name= inst->masterCell()->getName();
  cout << "-----" << inst->getName() <<endl;
  hcmNode* output_node;
  bool result;
  if(logic_name.find("nor")!=std::string::npos){
    result=!(logic_OR(inst,&output_node));
  } else if(logic_name.find("xor")!=std::string::npos){
    result=logic_XOR(inst,&output_node);
  } else if(logic_name.find("or")!=std::string::npos){
    result=logic_OR(inst,&output_node);
  } else if(logic_name.find("nand")!=std::string::npos){
    result=!(logic_AND(inst,&output_node));
  } else if(logic_name.find("and")!=std::string::npos){
    result=logic_AND(inst,&output_node);
  } else if(logic_name.find("buffer")!=std::string::npos){
    result=logic_BUFFER(inst,&output_node);
  } else if(logic_name.find("inv")!=std::string::npos){
    result=!(logic_BUFFER(inst,&output_node));
  } else if(logic_name.find("not")!=std::string::npos){
    result=!(logic_BUFFER(inst,&output_node));
  } else if(logic_name.find("dff")!=std::string::npos){
    result=DFF(inst,&output_node);
  } else{
    cerr << "-E- does not support gate type: " << logic_name << " aborting." << endl;
    exit(1);
  }
  cout<< result <<endl;
  cout<< "######" <<endl;
  //Applying the result to the output node
  bool prev_val ;
  // bool first_run;
  output_node->getProp("cur_val",prev_val);
  // output_node->getProp("first_run",first_run);
  // if output changes we push new event to EventQueue (Event-Driven approach)
  if( prev_val!=result){
    // if(first_run){
    //   output_node->setProp("first_run",false);
    // }
    Event new_event(output_node,result);
    EventQueue.push(new_event);
    // output_node->getProp("cur_val",result);
  }else{
    // cur val doesn't change
    output_node->setProp("prev_val",prev_val);
  }
}

void Event_Processor(std::queue< Event > &EventQueue,std::queue<hcmInstance *> &GateQueue){
  while(!EventQueue.empty()){
    Event E=EventQueue.front();
    hcmNode *node=E.Node;
    bool new_val = E.val;

    // copying new value to the Event's node and update previous value
    bool temp=false;
    if(node->getProp("cur_val",temp)==OK){
      node->setProp("prev_val",temp);
    }
    node->setProp("cur_val",new_val);

    // Iterating on Instances in the fanout of the node and pushing them to GateQueue
    std::map<std::string, hcmInstPort* >::const_iterator ipI;
    for (ipI =node->getInstPorts().begin(); ipI != node->getInstPorts().end(); ipI++){
      hcmInstPort *ip= ipI->second;
      hcmInstance *inst=ip->getInst();
      if(!Gate_Exist(GateQueue,inst->getName())){
        GateQueue.push(inst);
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
