#include "environment.h"
#include "../spatial/state.h"
#include <stdio.h>
#include <stdlib.h>
#include <GLES2/gl2.h>
#include <wlr/util/log.h>
#include "../../shady.h"
#include "../../render/gl_pipeline.h"
#include "../../assets/obj.h"
#include <string.h>

static GLuint prog, tex, vbo;
static GLint u_tex, u_right, u_up, u_forward, u_aspect;
static char loaded_path[512];
static GLuint mesh_prog, mesh_vbo;
static GLsizei mesh_vertex_count;
static GLint mesh_u_vp, mesh_u_light;
static char loaded_obj_path[512];

static GLuint shader(GLenum type, const char *s);


static bool make_mesh_program(void) {
	const char *vs="attribute vec3 a_pos; attribute vec3 a_normal; uniform mat4 u_vp; varying vec3 v_normal; void main(){v_normal=a_normal; gl_Position=u_vp*vec4(a_pos,1.); gl_Position.y=-gl_Position.y;}";
	const char *fs="precision mediump float; uniform vec3 u_light; varying vec3 v_normal; void main(){float d=max(dot(normalize(v_normal),normalize(u_light)),0.); vec3 c=vec3(.18,.28,.38)*(.28+.72*d); gl_FragColor=vec4(c,1.);}";
	GLuint a=shader(GL_VERTEX_SHADER,vs),b=shader(GL_FRAGMENT_SHADER,fs); if(!a||!b)return false;
	mesh_prog=glCreateProgram(); glAttachShader(mesh_prog,a); glAttachShader(mesh_prog,b);
	glBindAttribLocation(mesh_prog,0,"a_pos"); glBindAttribLocation(mesh_prog,1,"a_normal"); glLinkProgram(mesh_prog);
	glDeleteShader(a); glDeleteShader(b); GLint ok=0; glGetProgramiv(mesh_prog,GL_LINK_STATUS,&ok); if(!ok)return false;
	mesh_u_vp=glGetUniformLocation(mesh_prog,"u_vp"); mesh_u_light=glGetUniformLocation(mesh_prog,"u_light"); return true;
}
static bool load_obj_gpu(const char *path) {
	struct shady_mesh mesh;
	if(!shady_obj_load(path,&mesh))return false;
	if(mesh_vbo)glDeleteBuffers(1,&mesh_vbo);
	glGenBuffers(1,&mesh_vbo); glBindBuffer(GL_ARRAY_BUFFER,mesh_vbo);
	glBufferData(GL_ARRAY_BUFFER,(GLsizeiptr)(mesh.vertex_count*sizeof(*mesh.vertices)),mesh.vertices,GL_STATIC_DRAW);
	glBindBuffer(GL_ARRAY_BUFFER,0); mesh_vertex_count=(GLsizei)mesh.vertex_count;
	snprintf(loaded_obj_path,sizeof(loaded_obj_path),"%s",path);
	shady_mesh_fini(&mesh); return mesh_vbo!=0;
}

static GLuint shader(GLenum type,const char*s){GLuint x=glCreateShader(type);glShaderSource(x,1,&s,NULL);glCompileShader(x);GLint ok=0;glGetShaderiv(x,GL_COMPILE_STATUS,&ok);if(!ok){glDeleteShader(x);return 0;}return x;}
static bool make_program(void){
	const char*vs="attribute vec2 a_pos; varying vec2 v_uv; void main(){v_uv=a_pos*.5+.5; gl_Position=vec4(a_pos,0.,1.);}";
	const char*fs="precision mediump float; uniform sampler2D u_tex; uniform vec3 u_right,u_up,u_forward; uniform float u_aspect; varying vec2 v_uv; const float PI=3.14159265359; void main(){vec2 p=(v_uv*2.-1.); p.x*=u_aspect; vec3 d=normalize(vec3(p.x,-p.y,-2.145)); vec3 q=normalize(u_right*d.x + u_up*d.y + u_forward*(-d.z)); float u=atan(q.x,-q.z)/(2.*PI)+.5; float v=asin(clamp(q.y,-1.,1.))/PI+.5; gl_FragColor=texture2D(u_tex,vec2(u,1.-v));}";
	GLuint a=shader(GL_VERTEX_SHADER,vs),b=shader(GL_FRAGMENT_SHADER,fs);if(!a||!b)return false;prog=glCreateProgram();glAttachShader(prog,a);glAttachShader(prog,b);glBindAttribLocation(prog,0,"a_pos");glLinkProgram(prog);glDeleteShader(a);glDeleteShader(b);GLint ok=0;glGetProgramiv(prog,GL_LINK_STATUS,&ok);if(!ok)return false;u_tex=glGetUniformLocation(prog,"u_tex");u_right=glGetUniformLocation(prog,"u_right");u_up=glGetUniformLocation(prog,"u_up");u_forward=glGetUniformLocation(prog,"u_forward");u_aspect=glGetUniformLocation(prog,"u_aspect");static const GLfloat q[]={-1,-1,1,-1,-1,1,-1,1,1,-1,1,1};glGenBuffers(1,&vbo);glBindBuffer(GL_ARRAY_BUFFER,vbo);glBufferData(GL_ARRAY_BUFFER,sizeof(q),q,GL_STATIC_DRAW);glBindBuffer(GL_ARRAY_BUFFER,0);return true;
}
static bool load_ppm(const char*path){FILE*f=fopen(path,"rb");if(!f){wlr_log(WLR_ERROR,"sky: cannot open %s",path);return false;}char magic[3]={0};int w=0,h=0,max=0;if(fscanf(f,"%2s",magic)!=1||magic[0]!='P'||magic[1]!='6'){fclose(f);wlr_log(WLR_ERROR,"sky: first version supports binary PPM (P6)");return false;}int c=fgetc(f);while(c=='#'){while(c!='\n'&&c!=EOF)c=fgetc(f);c=fgetc(f);}ungetc(c,f);if(fscanf(f,"%d %d %d",&w,&h,&max)!=3||w<=0||h<=0||max!=255){fclose(f);return false;}fgetc(f);size_t n=(size_t)w*h*3;unsigned char*p=malloc(n);if(!p||fread(p,1,n,f)!=n){free(p);fclose(f);return false;}fclose(f);if(tex)glDeleteTextures(1,&tex);glGenTextures(1,&tex);glBindTexture(GL_TEXTURE_2D,tex);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MIN_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_MAG_FILTER,GL_LINEAR);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_S,GL_REPEAT);glTexParameteri(GL_TEXTURE_2D,GL_TEXTURE_WRAP_T,GL_CLAMP_TO_EDGE);
/* RGB rows are 3*w bytes. GLES defaults to 4-byte unpack alignment, which
 * corrupts PPMs whose row size is not divisible by four (e.g. 330px wide). */
glPixelStorei(GL_UNPACK_ALIGNMENT,1);glTexImage2D(GL_TEXTURE_2D,0,GL_RGB,w,h,0,GL_RGB,GL_UNSIGNED_BYTE,p);free(p);snprintf(loaded_path,sizeof(loaded_path),"%s",path);wlr_log(WLR_INFO,"sky: loaded %s (%dx%d)",path,w,h);return true;}
bool shady_environment_init(struct shady_gl_pipeline*p){(void)p;return make_program()&&make_mesh_program();}
bool shady_environment_load_colliders(struct shady_server*s){
	if(!s->config.environment_obj||!s->config.environment_obj_path[0])return true;
	struct shady_box_collider boxes[SHADY_WORLD_MAX_COLLIDERS]; size_t n=0;
	if(!shady_obj_load_colliders(s->config.environment_obj_path,boxes,SHADY_WORLD_MAX_COLLIDERS,&n))return false;
	for(size_t i=0;i<n;i++)if(!shady_world_add_collider(&shady_spatial_state(s)->runtime.world,boxes[i]))return false;
	struct shady_triangle_collider tris[SHADY_WORLD_MAX_TRIANGLES];size_t tn=0;
	if(!shady_obj_load_collision_triangles(s->config.environment_obj_path,
			tris,SHADY_WORLD_MAX_TRIANGLES,&tn))return false;
	for(size_t i=0;i<tn;i++)if(!shady_world_add_triangle(&shady_spatial_state(s)->runtime.world,&tris[i]))return false;
	wlr_log(WLR_INFO,"[SHADY COLLISION] ✅ WORLD READY: %zu OBJ groups / %zu triangles registered",n,tn);
	return true;
}
void shady_environment_fini(void){if(mesh_vbo)glDeleteBuffers(1,&mesh_vbo);if(mesh_prog)glDeleteProgram(mesh_prog);if(tex)glDeleteTextures(1,&tex);if(vbo)glDeleteBuffers(1,&vbo);if(prog)glDeleteProgram(prog);mesh_vbo=mesh_prog=tex=vbo=prog=0;mesh_vertex_count=0;loaded_obj_path[0]=0;loaded_path[0]=0;}
void shady_environment_draw(struct shady_server*s,struct shady_gl_pipeline*p,const float view[16],const float proj[16]){(void)p;(void)view;(void)proj;if(!s->config.sky||!s->config.sky_path[0])return;if(!tex||strcmp(loaded_path,s->config.sky_path))if(!load_ppm(s->config.sky_path))return;GLint vp[4];glGetIntegerv(GL_VIEWPORT,vp);glDisable(GL_DEPTH_TEST);glDepthMask(GL_FALSE);glUseProgram(prog);glUniform1i(u_tex,0);struct shady_vec3 r,u,f;shady_camera_basis(&shady_spatial_state(s)->runtime.camera,&r,&u,&f);glUniform3f(u_right,r.x,r.y,r.z);glUniform3f(u_up,u.x,u.y,u.z);glUniform3f(u_forward,f.x,f.y,f.z);glUniform1f(u_aspect,vp[3]?((float)vp[2]/vp[3]):1.f);glActiveTexture(GL_TEXTURE0);glBindTexture(GL_TEXTURE_2D,tex);glBindBuffer(GL_ARRAY_BUFFER,vbo);glVertexAttribPointer(0,2,GL_FLOAT,GL_FALSE,0,0);glEnableVertexAttribArray(0);glDrawArrays(GL_TRIANGLES,0,6);glDisableVertexAttribArray(0);glBindBuffer(GL_ARRAY_BUFFER,0);glUseProgram(0);glDepthMask(GL_TRUE);glEnable(GL_DEPTH_TEST);}

void shady_environment_draw_mesh(struct shady_server*s,const float vp[16]){
	if(!s->config.environment_obj||!s->config.environment_obj_path[0])return;
	if(!mesh_vbo||strcmp(loaded_obj_path,s->config.environment_obj_path))
		if(!load_obj_gpu(s->config.environment_obj_path))return;
	glUseProgram(mesh_prog); glUniformMatrix4fv(mesh_u_vp,1,GL_FALSE,vp); glUniform3f(mesh_u_light,-.45f,.72f,.53f);
	glDisable(GL_BLEND); glDepthMask(GL_TRUE); glBindBuffer(GL_ARRAY_BUFFER,mesh_vbo);
	glVertexAttribPointer(0,3,GL_FLOAT,GL_FALSE,sizeof(struct shady_mesh_vertex),(void*)offsetof(struct shady_mesh_vertex,position));
	glEnableVertexAttribArray(0);
	glVertexAttribPointer(1,3,GL_FLOAT,GL_FALSE,sizeof(struct shady_mesh_vertex),(void*)offsetof(struct shady_mesh_vertex,normal));
	glEnableVertexAttribArray(1); glDrawArrays(GL_TRIANGLES,0,mesh_vertex_count);
	glDisableVertexAttribArray(1); glDisableVertexAttribArray(0); glBindBuffer(GL_ARRAY_BUFFER,0); glUseProgram(0);
}
