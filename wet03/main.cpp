#include <errno.h>
#include <signal.h>
#include <sstream>
#include <fstream>
#include "hcm.h"
#include "flat.h"
#include <iostream>
#include <string> 
#include <sstream>

#define  __STDC_LIMIT_MACROS
#define  __STDC_FORMAT_MACROS
#include "utils/System.h"
#include "utils/ParseUtils.h"
#include "utils/Options.h"
#include "core/Dimacs.h"
#include "core/Solver.h"

using namespace Minisat;
using namespace std;

bool verbose = false;

///////////////////////////////////////////////////////////////////////////

/* functions declarations */
void Instance_Add_Clauses(hcmInstance* inst,Solver &S,ofstream& file,int &num_clauses);
bool compatible_cells(hcmCell *flatCell_spec,hcmCell *flatCell_imp, 
  std::map< hcmNode*, hcmNode* > &outputs_cells,std::map< hcmInstance*, hcmInstance* > &dff_cells);
void add_dffs_to_map(hcmInstance* spec_inst,hcmInstance* imp_inst,std::map< hcmNode*, hcmNode* > &outputs_cells);
void XOR_outputs(std::map< hcmNode*, hcmNode* > &outputs_cells,Solver &S,ofstream& file,int &num_clauses);

/* implementation in the end  */

int main(int argc, char **argv) {
  int argIdx = 1;
  int anyErr = 0;

  vector<string> specFiles;
  vector<string> implementFiles;
  
  if (argc < 7) {
    anyErr++;
  } 
  else {
    if (!strcmp(argv[argIdx], "-v")) {
          argIdx++;
          verbose = true;
    }
    if (!strcmp(argv[argIdx], "-s")) {
      argIdx++;
      // spec files
      for (;argIdx < argc; argIdx++) {
        if(!strcmp(argv[argIdx], "-i")){
          argIdx++;
          break;
        }
        specFiles.push_back(string(argv[argIdx]));
      }
      if(argIdx==argc){
        cerr << "-E- missing `-i` in the command line  " << endl;
        anyErr++;
      }
      if (specFiles.size() < 2) {
        cerr << "-E- At least spec-cell and single verilog file required for spec model" << endl;
        anyErr++;
      }
      // implementation files
      for (;argIdx < argc; argIdx++) {
        implementFiles.push_back(string(argv[argIdx]));
      }
      if (implementFiles.size() < 2) {
        cerr << "-E- At least implementation-cell and single verilog file required for implementaion model" << endl;
        anyErr++;
      }
    }
    else{
      cerr << "-E- missing `-s` in the command line" << endl;
      anyErr++;
    }
    
  }
  if (anyErr) {
    cerr << "Usage: " << argv[0] << "  [-v] -s spec-cell verilog1 [verilog2...] -i implemenattion-cell verilog1 [verilog2...] \n" ;
    exit(1);
  }
  

  set< string> globalNodes;
  globalNodes.insert("VDD");
  globalNodes.insert("VSS");
  

  // SPEC design
  hcmDesign* design_spec = new hcmDesign("design_spec");
  string cellName_spec = specFiles[0];
  for (unsigned int i = 1; i < specFiles.size(); i++) {
    printf("-I- (SPEC) Parsing verilog %s ...\n", specFiles[i].c_str());
    // cout<<  <<endl;
    if (!design_spec->parseStructuralVerilog(specFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << specFiles[i] << " aborting." << endl;
      exit(1);
    }
  }
  hcmCell *topCell_spec = design_spec->getCell(cellName_spec);
  if (!topCell_spec) {
    printf("-E- could not find cell %s\n", cellName_spec.c_str());
    exit(1);
  }

  // IMPLEMENTATION design
  hcmDesign* design_imp = new hcmDesign("design_imp");
  string cellName_imp = implementFiles[0];
  for (unsigned int i = 1; i < implementFiles.size(); i++) {
    printf("-I- (IMPLEMENTATION) Parsing verilog %s ...\n", implementFiles[i].c_str());
    if (!design_imp->parseStructuralVerilog(implementFiles[i].c_str())) {
      cerr << "-E- Could not parse: " << implementFiles[i] << " aborting." << endl;
      exit(1);
    }
  }
  hcmCell *topCell_imp = design_imp->getCell(cellName_imp);
  if (!topCell_imp) {
    printf("-E- could not find cell %s\n", cellName_imp.c_str());
    exit(1);
  }

  /*direct to file*/
  string fileName = specFiles[0] + string(".cnf");
  ofstream cnf_file(fileName.c_str());
  if (!cnf_file.good()) {
    cerr << "-E- Could not open file:" << fileName << endl;
    exit(1);
  }
  string tempFilename = string("temp_file.txt"); //temporary axuiliary file, later it will be deleted
  ofstream temp_file(tempFilename.c_str());
  if (!temp_file.good()) {
    cerr << "-E- Could not open file:" << tempFilename << endl;
    exit(1);
  }

  // Flattening the topcells
  hcmCell *flatCell_spec = hcmFlatten(cellName_spec + string("_flat"), topCell_spec, globalNodes);
  cout << "-I- Spec-Cell flattened" << endl;
  hcmCell *flatCell_imp = hcmFlatten(cellName_imp + string("_flat"), topCell_imp, globalNodes);
  cout << "-I- implementaion-Cell flattened" << endl;

  // map to save pairs of outputs nodes of each cell (those which are needed to be compared)
  std::map< hcmNode*, hcmNode* > outputs_cells; // first - spec node , second - implementation node.
  // map to save pairs of DFF of each cell.
  std::map< hcmInstance*, hcmInstance* > dff_cells; // first - spec dff , second - implementation dff.

  // check if cells are compatible for FEV, and if so push all output ports to the above map
  bool compatible = compatible_cells(flatCell_spec,flatCell_imp,outputs_cells,dff_cells);
  if(!compatible){
    cerr<<"-E Cells aren't compatible for FEV , aborting" <<endl;
    exit(1);
  }

 
  // introduce a number for each node (0,1,2,3...) for the SPEC cell
  std::map< std::string, hcmNode* >::const_iterator nI;
  std::map< std::string, hcmNode* > spec_nodes = flatCell_spec->getNodes();
  int var_num=0, num_nodes=spec_nodes.size();
  int vdd_num = num_nodes-1, vss_num= num_nodes-2;

  for (nI =spec_nodes.begin(); nI != spec_nodes.end(); nI++){
    hcmNode *node= nI->second;   
    if(node->getName()=="VDD"){
      node->setProp("variable_num",vdd_num);  
    }
    else if(node->getName()=="VSS"){ 
      node->setProp("variable_num",vss_num); 
    }
    else{
      int t;
      node->setProp("variable_num",var_num); 
      var_num++;
    }
  }
  


  //Adding inputs nodes of the same DFF to the outputs map,
  //and update implementation variable number of dff outputs
  //(we want dff outputs of both cells to have the same variable numbering).
  std::map< hcmInstance*, hcmInstance* >::const_iterator it_dff;
  for(it_dff=dff_cells.begin(); it_dff!=dff_cells.end(); it_dff++){
    hcmInstance* inst_spec = it_dff->first;
    hcmInstance* inst_imp = it_dff->second;
    add_dffs_to_map(inst_spec,inst_imp,outputs_cells);
  }

  // At the IMPLEMENTATION cell, match common nodes from the SPEC cell
  // and introduce new numbers for non-input nodes (and non-DFF output).
  // note that for input node (or output of DFF) which we've already created a variable
  // in the SPEC cell ,we gave the same variable numeber.
  var_num=num_nodes;
  std::map< std::string, hcmNode* > imp_nodes = flatCell_imp->getNodes();
  for (nI =imp_nodes.begin(); nI != imp_nodes.end(); nI++){
    hcmNode *node= nI->second;
    int t;
    int found = node->getProp("variable_num",t);
    if(found!=NOT_FOUND){ // already gave a value for DFFs outputs (the same in both cells) 
      // cout << "existed " << node->getName() << endl;
      continue;
    }
    string node_name=node->getName();
    if(node_name=="VSS" || node_name=="VDD"){
      int glob = vss_num+(node_name=="VDD");
      node->setProp("variable_num",glob);
      continue;
    }
    hcmPort *port = node->getPort();
    if(port && port->getDirection()==IN){
      hcmNode *node_from_spec = flatCell_spec->getNode(node_name);
      if(node_from_spec){  // a variable with the same name exist in the spec_cell
        int temp=0;
        node_from_spec->getProp("variable_num",temp);
        node->setProp("variable_num",temp);
      }
      else{
        node->setProp("variable_num",var_num); 
        var_num++;
      } 
    }
    else{
      node->setProp("variable_num",var_num); 
      var_num++;
    }
   
  } 

  cout<<"\nVariable mapping for each cell :"<<endl;
  cout << " ---- SPEC cell : " <<endl;
  std::map<int, string> input_var_to_name;
  for (nI =spec_nodes.begin(); nI != spec_nodes.end(); nI++){
    hcmNode *node= nI->second;
    int temp=0;
    node->getProp("variable_num",temp); 
    string name = node->getName();
    cout<<name<< " = " <<temp <<endl;
    hcmPort *port = node->getPort();
    if(port && port->getDirection()==IN){
      input_var_to_name.insert(std::pair<int, string>(temp,name));
    }
  } 
  cout << " ---- IMPLEMENTATION cell : " <<endl;
  for (nI =imp_nodes.begin(); nI != imp_nodes.end(); nI++){
    hcmNode *node= nI->second;
    int temp=0;
    node->getProp("variable_num",temp); 
    string name = node->getName();
    cout<<name<< " = " <<temp <<endl;
    hcmPort *port = node->getPort();
    if(port && port->getDirection()==IN){
      input_var_to_name.insert(std::pair<int, string>(temp,name));
    } 
  } 

  cout <<" "<<endl;
  Solver S;
  // Declare all the variables (which is currently var_num-1 vars)
  for(int i=0;i<var_num;i++){
    S.newVar();
  }
  // Forcing VDD=1 , VSS=0
  S.addClause(~mkLit(vss_num));
  S.addClause(mkLit(vdd_num));
  temp_file<<-(vss_num+1)<< " 0" <<endl;
  temp_file<<(vdd_num+1)<< " 0" <<endl;

  //number of variable is var_num + number of outputs +1 (we will introduce new variable later for each xor of outputs and for the OR between all XORs)
  // int nVars= var_num + outputs_cells.size()+1;

  int num_clauses= 2; // 2 for VDD and VSS clauses

  // creating appropriate tsyitin clauses to each instance in each of the cells
  std::map< std::string, hcmInstance* >::const_iterator iI;
  std::map< std::string, hcmInstance* > instances_spec = flatCell_spec->getInstances();
  for(iI =instances_spec.begin(); iI != instances_spec.end(); iI++){
    hcmInstance* inst= iI->second;
    Instance_Add_Clauses(inst,S,temp_file,num_clauses);
  }
  std::map< std::string, hcmInstance* > instances_imp = flatCell_imp->getInstances();
  for(iI =instances_imp.begin(); iI != instances_imp.end(); iI++){
    hcmInstance* inst= iI->second;
    Instance_Add_Clauses(inst,S,temp_file,num_clauses);
  }
 
  // Adding appropriate tsyitin clauses to each output (including DFF inputs)
  // in other words: xor clause between appropriate outputs (DFF inputs as well)
  XOR_outputs(outputs_cells,S,temp_file,num_clauses);
  cout << "Statistics:  "<<endl;
  cout << "   Number of clauses (before simplification):  " <<num_clauses <<endl;
  int nVars= S.nVars();
  cout << "   Number of variables:  " <<S.nVars() <<"\n"<<endl;


  cout << "Result:  "<<endl;
  // solve with minisat
  S.simplify();
  bool sat = S.solve();
  if(sat){
    cout << " -- SATISFIABLE - The circuits are different!" <<endl;
    cout << "Input assignment :" <<endl;
    // for(int i=0; i<var_num;i++){
    //   printf("%d = %s\n",i, (S.model[i]== l_Undef) ? "undef" : ((S.model[i]== l_True) ? "+" : "-"));
    // }
    std::map< int, string >::const_iterator inpuI;
    for(inpuI=input_var_to_name.begin(); inpuI!=input_var_to_name.end();inpuI++){
      string input_name = inpuI->second;
      int i = inpuI->first;
      cout << input_name;
      printf(" = %s\n", (S.model[i]== l_Undef) ? "undef" : ((S.model[i]== l_True) ? "+" : "-"));
    }
  } 
  else{
    cout << " -- UNSAT - The circuits are eqeuivalent" <<endl;
  }
  
  //first line in cnf file
  cnf_file << "p cnf "<< nVars << " "<< num_clauses <<endl;
  // copying the clauses in temp_file to cnf_file
  ifstream temp_f(tempFilename.c_str());
  std::string line;
  if(temp_f && cnf_file){
    while(getline(temp_f,line)){
        cnf_file << line << "\n";
    }
  } 

  if(cnf_file.is_open()) cnf_file.close();
  if(temp_file.is_open()) temp_file.close();
  temp_f.close();

  remove(tempFilename.c_str()); //remove the temporary file
  return(0);
}

// Function's implementations

/*
  Add xor clauses between outputs of cells, add introduce new variable as output
  of the xor. 
  In addition we add  more clause with OR of all the XOR outputs (as if one of them is 1 the problem is SAT).
  And one more clause to force the result of OR to be 1 (as shown in class).
*/
void XOR_outputs(std::map< hcmNode*, hcmNode* > &outputs_cells,Solver &S,ofstream& file,int &num_clauses){
  std::map< hcmNode*, hcmNode* >::const_iterator I;
  vector<int> XOR_results;
  for(I =outputs_cells.begin(); I != outputs_cells.end(); I++){
    hcmNode* spec_node = I->first;
    hcmNode* imp_node = I->second;
    int v = S.newVar();
    XOR_results.push_back(v);
    int a,b;
    spec_node->getProp("variable_num",a);
    imp_node->getProp("variable_num",b);

    // v = a xor b   clause
    S.addClause(~mkLit(a),~mkLit(b),~mkLit(v)); // (~A+ ~B+ ~V)
    S.addClause(mkLit(a),mkLit(b),~mkLit(v)); // (A+ B+ ~V)
    S.addClause(mkLit(a),~mkLit(b),mkLit(v)); // (A+ ~B+ V)
    S.addClause(~mkLit(a),mkLit(b),mkLit(v)); // (~A+ B+ V)
    file << -(a+1) <<" "<<-(b+1) << " " << -(v+1)<<" 0" <<endl;
    file << (a+1) <<" "<<(b+1) << " " << -(v+1)<<" 0" <<endl;
    file << (a+1) <<" "<<-(b+1) << " " << (v+1)<<" 0" <<endl;
    file << -(a+1) <<" "<<(b+1) << " " << (v+1)<<" 0" <<endl;
    num_clauses+=4;

  }

  int OR_result = S.newVar();
  // clause of OR between all XOR's outputs
  vec<Lit> clauseLiterals;
  std::ostringstream  clause ;
  clauseLiterals.push(~mkLit(OR_result));
  clause << -(OR_result+1) << " ";
  
  std::vector<int>::const_iterator it;
  for(it=XOR_results.begin(); it!=XOR_results.end(); it++){
    int var = *it;
    clauseLiterals.push(mkLit(var));
    clause << (var+1) << " ";
    S.addClause(mkLit(OR_result),~mkLit(var));
    file << (OR_result+1) <<" "<<-(var+1) <<" 0" <<endl;
    num_clauses++;

  }
  S.addClause(clauseLiterals);
  clause << "0";
  file << clause.str() <<endl;
  clauseLiterals.clear();

  // forcing OR result to be 1
  S.addClause(mkLit(OR_result));
  file << (OR_result+1) <<" 0" <<endl;
  num_clauses+=2;
}


/*
 in DFF we consider the inputs as outputs which need to be compared :
 we add pairs of the same input (by name of the port) to the output map.
*/
void add_dffs_to_map(hcmInstance* spec_inst,hcmInstance* imp_inst,std::map< hcmNode*, hcmNode* > &outputs_cells){
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  std::map<std::string, hcmInstPort* >spec_inst_ports =spec_inst->getInstPorts();
  std::map<std::string, hcmInstPort* >imp_inst_ports =imp_inst->getInstPorts();
  for (ipI =spec_inst_ports.begin(); ipI != spec_inst_ports.end(); ipI++){
    hcmInstPort* ip_spec= ipI->second;
    hcmNode* node_spec= ip_spec->getNode();
    std::map<std::string, hcmInstPort* >::const_iterator I;
    if(ip_spec->getPort()->getDirection()==IN){
      for (I =imp_inst_ports.begin(); I != imp_inst_ports.end(); I++){
        hcmInstPort* ip_imp= I->second;
        if(ip_imp->getPort()->getName()==ip_spec->getPort()->getName()){
          hcmNode* node_imp= ip_imp->getNode();
          outputs_cells.insert(std::pair<hcmNode*, hcmNode*>(node_spec,node_imp));
        }
      }
    } 
    if(ip_spec->getPort()->getDirection()==OUT){
      for (I =imp_inst_ports.begin(); I != imp_inst_ports.end(); I++){
        hcmInstPort* ip_imp= I->second;
        if(ip_imp->getPort()->getName()==ip_spec->getPort()->getName()){
          hcmNode* node_imp= ip_imp->getNode();
          int temp=0;
          node_spec->getProp("variable_num",temp);
          node_imp->setProp("variable_num",temp); // give imp dff output the same variable as in spec (it serves as "input")
          // cout << "updated " << temp << endl;
        }
      }
    }
  }
}

void logic_Inverter(hcmInstance* inst,Solver &S,bool inverted,ofstream& file){
  int input_var, output_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      node->getProp("variable_num",input_var);
    }
  }
  // cout << output_var <<endl;
  // cout << input_var <<endl;
  if(!inverted){
    S.addClause(mkLit(output_var),mkLit(input_var));
    S.addClause(~mkLit(output_var),~mkLit(input_var));
    file << (output_var+1) <<" "<<(input_var+1) << " 0" <<endl;
    file << -(output_var+1) <<" "<<-(input_var+1) << " 0" <<endl;    
  }
  else{
    S.addClause(~mkLit(output_var),mkLit(input_var));
    S.addClause(mkLit(output_var),~mkLit(input_var));
    file << -(output_var+1) <<" "<<(input_var+1) << " 0" <<endl;
    file << (output_var+1) <<" "<<-(input_var+1) << " 0" <<endl;  
  }

}


void logic_AND(hcmInstance* inst,Solver &S, bool inverted,ofstream& file,int &num_clauses){
  int output_var;
  vector<int> input_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      int input;
      node->getProp("variable_num",input);
      input_var.push_back(input);
    }
  }

  std::ostringstream  clause ;

  //Building Tseytin clauses
  vec<Lit> clauseLiterals;
  if(!inverted) {
    clauseLiterals.push(mkLit(output_var));
    clause << (output_var+1) << " ";
  }
  else {
    clauseLiterals.push(~mkLit(output_var));
    clause << -(output_var+1) << " ";
  }
  
  std::vector<int>::const_iterator it;
  for(it=input_var.begin(); it!=input_var.end(); it++){
    int var = *it;
    // cout << var <<endl;
    clauseLiterals.push(~mkLit(var));
    clause << -(var+1) << " ";
    if(!inverted) {
      S.addClause(~mkLit(output_var),mkLit(var));
      file << -(output_var+1) <<" "<<(var+1) << " 0" <<endl;
    }
    else{ 
      S.addClause(mkLit(output_var),mkLit(var));
      file << (output_var+1) <<" "<<(var+1) << " 0" <<endl;
    }
    num_clauses++;
  }
  S.addClause(clauseLiterals);
  num_clauses++;
  clause << "0";
  file << clause.str() <<endl;

  clauseLiterals.clear();
}

void logic_OR(hcmInstance* inst,Solver &S, bool inverted,ofstream& file,int &num_clauses){
  int output_var;
  vector<int> input_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      int input;
      node->getProp("variable_num",input);
      input_var.push_back(input);

    }
  }
  std::ostringstream  clause ;

  //Building Tseytin clauses
  vec<Lit> clauseLiterals;
  if(!inverted) {
    clauseLiterals.push(~mkLit(output_var));
    clause << -(output_var+1) << " ";
  }
  else {
    clauseLiterals.push(mkLit(output_var));
    clause << (output_var+1) << " ";
  }
  
  std::vector<int>::const_iterator it;
  for(it=input_var.begin(); it!=input_var.end(); it++){
    int var = *it;
    // cout << var <<endl;
    clauseLiterals.push(mkLit(var));
    clause << (var+1) << " ";
    if(!inverted) {
      S.addClause(mkLit(output_var),~mkLit(var));
      file << (output_var+1) <<" "<<-(var+1) << " 0" <<endl;
    }
    else {
      S.addClause(~mkLit(output_var),~mkLit(var));
      file << -(output_var+1) <<" "<<-(var+1) << " 0" <<endl;
    }
    num_clauses++;
  }
  S.addClause(clauseLiterals);
  num_clauses++;
  clause << "0";
  file << clause.str() <<endl;
  clauseLiterals.clear();
}

void logic_XOR(hcmInstance* inst,Solver &S, bool inverted,ofstream& file){
  int output_var;
  vector<int> input_var;
  std::map<std::string, hcmInstPort* >::const_iterator ipI;
  for (ipI =inst->getInstPorts().begin(); ipI != inst->getInstPorts().end(); ipI++){
    hcmInstPort* ip= ipI->second;
    hcmNode* node= ip->getNode();
    if(ip->getPort()->getDirection()==OUT){
      node->getProp("variable_num",output_var);
    }
    if(ip->getPort()->getDirection()==IN){
      int input;
      node->getProp("variable_num",input);
      input_var.push_back(input);
    }
  }
  //Building Tseytin clauses
  if(!inverted){
    S.addClause(~mkLit(input_var[0]),~mkLit(input_var[1]),~mkLit(output_var)); // (~A+ ~B+ ~C)
    S.addClause(mkLit(input_var[0]),mkLit(input_var[1]),~mkLit(output_var)); // (A+ B+ ~C)
    S.addClause(mkLit(input_var[0]),~mkLit(input_var[1]),mkLit(output_var)); // (A+ ~B+ C)
    S.addClause(~mkLit(input_var[0]),mkLit(input_var[1]),mkLit(output_var)); // (~A+ B+ C)
    file << -(input_var[0]+1) <<" "<<-(input_var[1]+1) << " " << -(output_var+1)<<" 0" <<endl;
    file << (input_var[0]+1) <<" "<<(input_var[1]+1) << " " << -(output_var+1)<<" 0" <<endl;
    file << (input_var[0]+1) <<" "<<-(input_var[1]+1) << " " << (output_var+1)<<" 0" <<endl;
    file << -(input_var[0]+1) <<" "<<(input_var[1]+1) << " " << (output_var+1)<<" 0" <<endl;
  }
  else{
    S.addClause(~mkLit(input_var[0]),~mkLit(input_var[1]),mkLit(output_var)); // (~A+ ~B+ C)
    S.addClause(mkLit(input_var[0]),mkLit(input_var[1]),mkLit(output_var)); // (A+ B+ C)
    S.addClause(mkLit(input_var[0]),~mkLit(input_var[1]),~mkLit(output_var)); // (A+ ~B+ ~C)
    S.addClause(~mkLit(input_var[0]),mkLit(input_var[1]),~mkLit(output_var)); // (~A+ B+ ~C)
    file << -(input_var[0]+1) <<" "<<-(input_var[1]+1) << " " << (output_var+1)<<" 0" <<endl;
    file << (input_var[0]+1) <<" "<<(input_var[1]+1) << " " << (output_var+1)<<" 0" <<endl;
    file << (input_var[0]+1) <<" "<<-(input_var[1]+1) << " " << -(output_var+1)<<" 0" <<endl;
    file << -(input_var[0]+1) <<" "<<(input_var[1]+1) << " " << -(output_var+1)<<" 0" <<endl;
  }
}

void Instance_Add_Clauses(hcmInstance* inst,Solver &S,ofstream& file,int &num_clauses){
  string logic_name= inst->masterCell()->getName();
  if(logic_name.find("nor")!=std::string::npos){
    logic_OR(inst,S,true,file,num_clauses);
    // cout<< "$nor" <<endl;
  } else if(logic_name.find("xnor")!=std::string::npos){
    logic_XOR(inst,S,true,file);
    num_clauses+=4;//supported only xor 2
    // cout<< "$xnor" <<endl;
  } else if(logic_name.find("xor")!=std::string::npos){
    logic_XOR(inst,S,false,file);
    num_clauses+=4;//supported only xor 2
    // cout<< "$xor" <<endl;
  } else if(logic_name.find("or")!=std::string::npos){
    logic_OR(inst,S,false,file,num_clauses);
    // cout<< "$or" <<endl;
  } else if(logic_name.find("nand")!=std::string::npos){
    logic_AND(inst,S,true,file,num_clauses);
    // cout<< "$nand" <<endl;
  } else if(logic_name.find("and")!=std::string::npos){
    logic_AND(inst,S,false,file,num_clauses);
    // cout<< "$and" <<endl;
  } else if(logic_name.find("buffer")!=std::string::npos){
    logic_Inverter(inst,S,true,file); //buffer is an inverted inverter :)
    num_clauses+=2;
    // cout<< "$buffer" <<endl;
  } else if(logic_name.find("inv")!=std::string::npos){
    logic_Inverter(inst,S,false,file);
    num_clauses+=2;
    // cout<< "$inv" <<endl;
  } else if(logic_name.find("not")!=std::string::npos){
    logic_Inverter(inst,S,false,file);
    num_clauses+=2;
    // cout<< "$inv" <<endl;
  } else if(logic_name.find("dff")!=std::string::npos){
    // remember DFF inputs (used as "outputs") are needed to be compared between the cells 
    // however no logic functioning for dff so we do nothing here
  } else{
    cerr << "-E- does not support gate type: " << logic_name << " aborting." << endl;
    exit(1);
  }
}



//return the hcminstance* of the dff from the imp_cell
//if it has an absoluto name "inst_name", if not found return NULL;
hcmInstance* DFF_exist_in_implementation(string inst_name,std::map< std::string, hcmInstance* > &instances_imp){
  std::map< std::string, hcmInstance* >::const_iterator iI;
  for(iI =instances_imp.begin(); iI != instances_imp.end(); iI++){
    hcmInstance* inst_imp= iI->second;
    string imp_name= inst_imp->getName();
    std::size_t absolute_inst_name_idx= imp_name.find_last_of("/");
    string absolute_name = imp_name.substr(absolute_inst_name_idx+1); //get absolute instance name withouth complete path at flatten_Cell
    if(absolute_name==inst_name){
      return inst_imp;
    }
  }
  return NULL;
}


/*
  This function return true iff the spec cell and implementation cell
  have the exact amount of output ports, and each have same output names,
  and the same DFF amount and with same names (as their inputs will be considered as outputs).
  If one of the above doesn't happen, we return false as the cells are incompatible for FEV.
  It also update the dff map and output map.
*/

bool compatible_cells(hcmCell *flatCell_spec,hcmCell *flatCell_imp, 
  std::map< hcmNode*, hcmNode* > &outputs_cells,std::map< hcmInstance*, hcmInstance* > &dff_cells){
  //check if every output name match between the 2 cells
  int out_ports_spec =0 ,out_ports_imp=0 ;
  vector<hcmPort*> spec_ports = flatCell_spec->getPorts();
  std::vector<hcmPort*>::const_iterator pI;
  for(pI=spec_ports.begin(); pI!=spec_ports.end(); pI++){
    hcmPort* port_sp = *pI;
    if(port_sp->getDirection() == OUT){
      out_ports_spec++;
      string port_name =port_sp->getName();
      hcmPort * port_imp = flatCell_imp->getPort(port_name);
      if(!port_imp){
        cerr<<"-E Different names of output ports between the cells" <<endl;
        return false;
      }
      outputs_cells.insert(std::pair<hcmNode*, hcmNode*>(port_sp->owner(),port_imp->owner()));
    }
  }
  // check if both cells have the same amount of outputs ports
  vector<hcmPort*> imp_ports = flatCell_imp->getPorts();
  for(pI=imp_ports.begin(); pI!=imp_ports.end(); pI++){
    hcmPort* port_imp = *pI;
    if(port_imp->getDirection() == OUT){
      out_ports_imp++;
    }
  }
  if(out_ports_imp!=out_ports_spec) {
    cerr<<"-E Different amount of output ports between the cells" <<endl;
    return false;
  }

  //check if every DFF name match between the 2 cells
  int DFF_spec =0 ,DFF_imp=0 ;
  std::map< std::string, hcmInstance* >::const_iterator iI;
  std::map< std::string, hcmInstance* > instances_spec = flatCell_spec->getInstances();
  std::map< std::string, hcmInstance* > instances_imp = flatCell_imp->getInstances();
  for(iI =instances_spec.begin(); iI != instances_spec.end(); iI++){
    hcmInstance* inst_spec= iI->second;
    string logic_name= inst_spec->masterCell()->getName();
    if(logic_name.find("dff")!=std::string::npos){
      DFF_spec++;
      string inst_name= inst_spec->getName();
      std::size_t absolute_inst_name_idx= inst_name.find_last_of("/");
      string absolute_name = inst_name.substr(absolute_inst_name_idx+1); //get absolute instance name withouth complete path at flatten_Cell

      hcmInstance *inst_imp = DFF_exist_in_implementation(absolute_name,instances_imp);
      if(!inst_imp){
        cout << absolute_name << endl;
        cerr<<"-E Different names of DFF between the cells" <<endl;
        return false;
      }
      dff_cells.insert(std::pair<hcmInstance*, hcmInstance*>(inst_spec,inst_imp));
    }
  }
  // check if both cells have the same amount of DFF
  for(iI =instances_imp.begin(); iI != instances_imp.end(); iI++){
    hcmInstance* inst_imp= iI->second;
    string logic_name= inst_imp->masterCell()->getName();
    if(logic_name.find("dff")!=std::string::npos){
      DFF_imp++;
    }
  }
  if(DFF_imp!=DFF_spec) {
    cerr<<"-E Different amount of DFF between the cells" <<endl;
    return false;
  }
  // If haven't returned false yet, it means the cells are compatible for FEV
  return true;
}