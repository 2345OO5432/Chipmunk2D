/* Copyright (c) 2007 Scott Lembcke
 * 
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 * 
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 * 
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
 * SOFTWARE.
 */

#include <limits.h>
#include <string.h>

#ifdef __APPLE__
	#include "glew.h"
	#include "glfw.h"
#else
	#ifdef WIN32
		#include <windows.h>
	#endif
	
	#include <GL/gl.h>
	#include <GL/glu.h>
#endif

#include "chipmunk_private.h"
#include "ChipmunkDebugDraw.h"
#include "ChipmunkDemoShaderSupport.h"

/*
	IMPORTANT - READ ME!
	
	This file sets up a simple interface that the individual demos can use to get
	a Chipmunk space running and draw what's in it. In order to keep the Chipmunk
	examples clean and simple, they contain no graphics code. All drawing is done
	by accessing the Chipmunk structures at a very low level. It is NOT
	recommended to write a game or application this way as it does not scale
	beyond simple shape drawing and is very dependent on implementation details
	about Chipmunk which may change with little to no warning.
*/

const Color LINE_COLOR = {200.0/255.0, 210.0/255.0, 230.0/255.0, 1.0};
const Color CONSTRAINT_COLOR = {0.0, 0.75, 0.0, 1.0};
const float SHAPE_ALPHA = 1.0;

float ChipmunkDebugDrawPointLineScale = 1.0;
float ChipmunkDebugDrawOutlineWidth = 1.0;

static GLuint program;

typedef struct Vertex {cpVect vertex, aa_coord; Color fill_color, outline_color;} Vertex;
typedef struct Triangle {Vertex a, b, c;} Triangle;

static GLuint vao = 0;
static GLuint vbo = 0;

#define GLSL(x) #x

static void
SetAttribute(GLuint program, char *name, GLint size, GLenum gltype, GLsizei stride, GLvoid *offset)
{
	GLint index = glGetAttribLocation(program, name);
	glEnableVertexAttribArray(index);
	glVertexAttribPointer(index, size, gltype, GL_FALSE, stride, offset);
}

#define SET_ATTRIBUTE(program, type, name, gltype)\
	SetAttribute(program, #name, sizeof(((type *)NULL)->name)/sizeof(GLfloat), gltype, sizeof(type), (GLvoid *)offsetof(type, name))

void
ChipmunkDebugDrawInit(void)
{
	// Setup the AA shader.
	GLint vshader = CompileShader(GL_VERTEX_SHADER, GLSL(
		attribute vec2 vertex;
		attribute vec2 aa_coord;
		attribute vec4 fill_color;
		attribute vec4 outline_color;
		
		varying vec2 v_aa_coord;
		varying vec4 v_fill_color;
		varying vec4 v_outline_color;
		
		void main(void){
			// TODO get rid of the GL 2.x matrix bit eventually?
			gl_Position = gl_ModelViewProjectionMatrix*vec4(vertex, 0.0, 1.0);
			
			v_fill_color = fill_color;
			v_outline_color = outline_color;
			v_aa_coord = aa_coord;
		}
	));
	
	GLint fshader = CompileShader(GL_FRAGMENT_SHADER, GLSL(
		uniform float u_outline_coef;
		
		varying vec2 v_aa_coord;
		varying vec4 v_fill_color;
		//const vec4 v_fill_color = vec4(0.0, 0.0, 0.0, 1.0);
		varying vec4 v_outline_color;
		
		float aa_step(float t1, float t2, float f)
		{
			//return step(t2, f);
			return smoothstep(t1, t2, f);
		}
		
		void main(void)
		{
			float l = length(v_aa_coord);
			
			// Different pixel size estimations are handy.
			//float fw = fwidth(l);
			//float fw = length(vec2(dFdx(l), dFdy(l)));
			float fw = length(fwidth(v_aa_coord));
			
			// Outline width threshold.
			float ow = 1.0 - fw*u_outline_coef;
			
			// Fill/outline color.
			float fo_step = aa_step(ow - fw, ow, l);
			vec4 fo_color = mix(v_fill_color, v_outline_color, fo_step);
			
			// Use pre-multiplied alpha.
			float alpha = 1.0 - aa_step(1.0 - fw, 1.0, l);
			gl_FragColor = fo_color*(fo_color.a*alpha);
			//gl_FragColor = vec4(vec3(l), 1);
		}
	));
	
	program = LinkProgram(vshader, fshader);
	CHECK_GL_ERRORS();
	
	// Setu VBO and VAO.
	glGenVertexArraysAPPLE(1, &vao);
	glBindVertexArrayAPPLE(vao);
	
	glGenBuffers(1, &vbo);
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	
	GLenum cp_float_type = (CP_USE_DOUBLES ? GL_DOUBLE : GL_FLOAT);
	
	SET_ATTRIBUTE(program, struct Vertex, vertex, cp_float_type);
	SET_ATTRIBUTE(program, struct Vertex, aa_coord, cp_float_type);
	SET_ATTRIBUTE(program, struct Vertex, fill_color, GL_FLOAT);
	SET_ATTRIBUTE(program, struct Vertex, outline_color, GL_FLOAT);
	
	glBindBuffer(GL_ARRAY_BUFFER, 0);
	glBindVertexArrayAPPLE(0);
	
	CHECK_GL_ERRORS();
}


static Color
ColorFromHash(cpHashValue hash, float alpha)
{
	unsigned long val = (unsigned long)hash;
	
	// scramble the bits up using Robert Jenkins' 32 bit integer hash function
	val = (val+0x7ed55d16) + (val<<12);
	val = (val^0xc761c23c) ^ (val>>19);
	val = (val+0x165667b1) + (val<<5);
	val = (val+0xd3a2646c) ^ (val<<9);
	val = (val+0xfd7046c5) + (val<<3);
	val = (val^0xb55a4f09) ^ (val>>16);
	
	GLfloat r = (val>>0) & 0xFF;
	GLfloat g = (val>>8) & 0xFF;
	GLfloat b = (val>>16) & 0xFF;
	
	GLfloat max = cpfmax(cpfmax(r, g), b);
	GLfloat min = cpfmin(cpfmin(r, g), b);
	GLfloat intensity = 0.75;
	
	// Saturate and scale the color
	if(min == max){
		return RGBAColor(intensity, 0.0, 0.0, alpha);
	} else {
		GLfloat coef = intensity/(max - min);
		return RGBAColor(
			(r - min)*coef,
			(g - min)*coef,
			(b - min)*coef,
			alpha
		);
	}
}

static inline void
glColor_from_color(Color color){
	glColor4fv((GLfloat *)&color);
}

static Color
ColorForShape(cpShape *shape)
{
	if(cpShapeGetSensor(shape)){
		return LAColor(1.0f, 0.1f);
	} else {
		cpBody *body = shape->body;
		
		if(cpBodyIsSleeping(body)){
			return LAColor(0.2, 1);
		} else if(body->node.idleTime > shape->space->sleepTimeThreshold) {
			return LAColor(0.66, 1);
		} else {
			return ColorFromHash(shape->hashid, SHAPE_ALPHA);
		}
	}
}


static size_t triangle_capacity = 0;
static size_t triangle_count = 0;
static Triangle *triangle_buffer = NULL;

static Triangle *PushTriangles(size_t count)
{
	if(triangle_count + count > triangle_capacity){
		triangle_capacity += MAX(triangle_capacity, count);
		triangle_buffer = realloc(triangle_buffer, triangle_capacity*sizeof(Triangle));
	}
	
	Triangle *buffer = triangle_buffer + triangle_count;
	triangle_count += count;
	return buffer;
}


void ChipmunkDebugDrawCircle(cpVect pos, cpFloat angle, cpFloat radius, Color outlineColor, Color fillColor)
{
	Triangle *triangles = PushTriangles(2);
	
	cpFloat r = radius + ChipmunkDebugDrawPointLineScale*0.5f;
	Vertex a = {{pos.x - r, pos.y - r}, {-1.0, -1.0}, fillColor, outlineColor};
	Vertex b = {{pos.x - r, pos.y + r}, {-1.0,  1.0}, fillColor, outlineColor};
	Vertex c = {{pos.x + r, pos.y + r}, { 1.0,  1.0}, fillColor, outlineColor};
	Vertex d = {{pos.x + r, pos.y - r}, { 1.0, -1.0}, fillColor, outlineColor};
	
	triangles[0] = (Triangle){a, b, c};
	triangles[1] = (Triangle){a, c, d};
	
	ChipmunkDebugDrawSegment(pos, cpvadd(pos, cpvmult(cpvforangle(angle), radius - ChipmunkDebugDrawPointLineScale*0.5f)), outlineColor);
}

void ChipmunkDebugDrawSegment(cpVect a, cpVect b, Color color)
{
	ChipmunkDebugDrawFatSegment(a, b, 0.0f, color, color);
}

void ChipmunkDebugDrawFatSegment(cpVect a, cpVect b, cpFloat radius, Color outlineColor, Color fillColor)
{
	Triangle *triangles = PushTriangles(6);
	
	cpVect n = cpvnormalize(cpvperp(cpvsub(b, a)));
	cpVect t = cpvperp(n);
	
	cpFloat r = radius + ChipmunkDebugDrawPointLineScale*0.5f;
	if(r < ChipmunkDebugDrawPointLineScale){
		r = ChipmunkDebugDrawPointLineScale;
		fillColor = outlineColor;
	}
	
	cpVect nw = cpvmult(n, r);
	cpVect tw = cpvmult(t, r);
	cpVect v0 = cpvsub(b, cpvadd(nw, tw)); // { 1.0, -1.0}
	cpVect v1 = cpvadd(b, cpvsub(nw, tw)); // { 1.0,  1.0}
	cpVect v2 = cpvsub(b, nw); // { 0.0, -1.0}
	cpVect v3 = cpvadd(b, nw); // { 0.0,  1.0}
	cpVect v4 = cpvsub(a, nw); // { 0.0, -1.0}
	cpVect v5 = cpvadd(a, nw); // { 0.0,  1.0}
	cpVect v6 = cpvsub(a, cpvsub(nw, tw)); // {-1.0, -1.0}
	cpVect v7 = cpvadd(a, cpvadd(nw, tw)); // {-1.0,  1.0}
	
	triangles[0] = (Triangle){{v0, { 1.0f, -1.0f}, fillColor, outlineColor}, {v1, { 1.0f,  1.0f}, fillColor, outlineColor}, {v2, { 0.0f, -1.0f}, fillColor, outlineColor}};
	triangles[1] = (Triangle){{v3, { 0.0f,  1.0f}, fillColor, outlineColor}, {v1, { 1.0f,  1.0f}, fillColor, outlineColor}, {v2, { 0.0f, -1.0f}, fillColor, outlineColor}};
	triangles[2] = (Triangle){{v3, { 0.0f,  1.0f}, fillColor, outlineColor}, {v4, { 0.0f, -1.0f}, fillColor, outlineColor}, {v2, { 0.0f, -1.0f}, fillColor, outlineColor}};
	triangles[3] = (Triangle){{v3, { 0.0f,  1.0f}, fillColor, outlineColor}, {v4, { 0.0f, -1.0f}, fillColor, outlineColor}, {v5, { 0.0f,  1.0f}, fillColor, outlineColor}};
	triangles[4] = (Triangle){{v6, {-1.0f, -1.0f}, fillColor, outlineColor}, {v4, { 0.0f, -1.0f}, fillColor, outlineColor}, {v5, { 0.0f,  1.0f}, fillColor, outlineColor}};
	triangles[5] = (Triangle){{v6, {-1.0f, -1.0f}, fillColor, outlineColor}, {v7, {-1.0f,  1.0f}, fillColor, outlineColor}, {v5, { 0.0f,  1.0f}, fillColor, outlineColor}};
}

extern cpVect ChipmunkDemoMouse;

void ChipmunkDebugDrawPolygon(int count, cpVect *verts, cpFloat radius, Color outlineColor, Color fillColor)
{
	struct ExtrudeVerts {cpVect offset, n;};
	struct ExtrudeVerts extrude[count];
	bzero(extrude, sizeof(struct ExtrudeVerts)*count);
	
	for(int i=0; i<count; i++){
		cpVect v0 = verts[(i-1+count)%count];
		cpVect v1 = verts[i];
		cpVect v2 = verts[(i+1)%count];
		
		cpVect n1 = cpvnormalize(cpvperp(cpvsub(v1, v0)));
		cpVect n2 = cpvnormalize(cpvperp(cpvsub(v2, v1)));
		
		cpVect offset = cpvmult(cpvadd(n1, n2), 1.0/(cpvdot(n1, n2) + 1.0f));
		extrude[i] = (struct ExtrudeVerts){offset, n2};
	}
	
//	Triangle *triangles = PushTriangles(6*count);
	Triangle *triangles = PushTriangles(5*count - 2);
	Triangle *cursor = triangles;
	
	cpFloat inset = cpfmax(0.0f, ChipmunkDebugDrawPointLineScale - radius);
	for(int i=0; i<count-2; i++){
		cpVect v0 = cpvsub(verts[  0], cpvmult(extrude[  0].offset, inset));
		cpVect v1 = cpvsub(verts[i+1], cpvmult(extrude[i+1].offset, inset));
		cpVect v2 = cpvsub(verts[i+2], cpvmult(extrude[i+2].offset, inset));
		
		*cursor++ = (Triangle){{v0, cpvzero, fillColor, fillColor}, {v1, cpvzero, fillColor, fillColor}, {v2, cpvzero, fillColor, fillColor}};
	}
	
	cpFloat outset = inset + ChipmunkDebugDrawPointLineScale + radius;
	for(int i=0, j=count-1; i<count; j=i, i++){
		cpVect v0 = verts[i];
		cpVect v1 = verts[j];
		
		cpVect n0 = extrude[i].n;
		cpVect n1 = extrude[j].n;
		
		cpVect offset0 = extrude[i].offset;
		cpVect offset1 = extrude[j].offset;
		
		cpVect inner0 = cpvsub(v0, cpvmult(offset0, inset));
		cpVect inner1 = cpvsub(v1, cpvmult(offset1, inset));
		cpVect outer0 = cpvadd(inner0, cpvmult(n1, outset));
		cpVect outer1 = cpvadd(inner1, cpvmult(n1, outset));
		cpVect outer2 = cpvadd(inner0, cpvmult(offset0, outset));
		cpVect outer3 = cpvadd(inner0, cpvmult(n0, outset));
		
//		cpFloat fraction = inset/(inset + outset);
//		cpVect aa1 = cpvmult(n1, fraction);
//		cpVect aa2 = cpvmult(offset0, fraction);
//		cpVect aa3 = cpvmult(n0, fraction);
		
		*cursor++ = (Triangle){{inner0, cpvzero, fillColor, outlineColor}, {inner1, cpvzero, fillColor, outlineColor}, {outer1,      n1, fillColor, outlineColor}};
		*cursor++ = (Triangle){{inner0, cpvzero, fillColor, outlineColor}, {outer0,      n1, fillColor, outlineColor}, {outer1,      n1, fillColor, outlineColor}};
//		*cursor++ = (Triangle){{    v0,     aa1, fillColor, outlineColor}, {    v1,     aa1, fillColor, outlineColor}, {outer1,      n1, fillColor, outlineColor}};
//		*cursor++ = (Triangle){{    v0,     aa1, fillColor, outlineColor}, {outer0,      n1, fillColor, outlineColor}, {outer1,      n1, fillColor, outlineColor}};
		*cursor++ = (Triangle){{inner0, cpvzero, fillColor, outlineColor}, {outer0,      n1, fillColor, outlineColor}, {outer2, offset0, fillColor, outlineColor}};
		*cursor++ = (Triangle){{inner0, cpvzero, fillColor, outlineColor}, {outer2, offset0, fillColor, outlineColor}, {outer3,      n0, fillColor, outlineColor}};
	}
}

void ChipmunkDebugDrawDot(cpFloat size, cpVect pos, Color fillColor)
{
	Triangle *triangles = PushTriangles(2);
	
	float r = ChipmunkDebugDrawPointLineScale*size*0.5f;
	Vertex a = {{pos.x - r, pos.y - r}, {-1.0f, -1.0f}, fillColor, fillColor};
	Vertex b = {{pos.x - r, pos.y + r}, {-1.0f,  1.0f}, fillColor, fillColor};
	Vertex c = {{pos.x + r, pos.y + r}, { 1.0f,  1.0f}, fillColor, fillColor};
	Vertex d = {{pos.x + r, pos.y - r}, { 1.0f, -1.0f}, fillColor, fillColor};
	
	triangles[0] = (Triangle){a, b, c};
	triangles[1] = (Triangle){a, c, d};
}

void ChipmunkDebugDrawBB(cpBB bb, Color color)
{
	cpVect verts[] = {
		cpv(bb.l, bb.b),
		cpv(bb.l, bb.t),
		cpv(bb.r, bb.t),
		cpv(bb.r, bb.b),
	};
	ChipmunkDebugDrawPolygon(4, verts, 0.0f, color, LAColor(0, 0));
}

static void
drawShape(cpShape *shape, void *unused)
{
	cpBody *body = shape->body;
	Color color = ColorForShape(shape);
	
	switch(shape->klass->type){
		case CP_CIRCLE_SHAPE: {
			cpCircleShape *circle = (cpCircleShape *)shape;
			ChipmunkDebugDrawCircle(circle->tc, body->a, circle->r, LINE_COLOR, color);
			break;
		}
		case CP_SEGMENT_SHAPE: {
			cpSegmentShape *seg = (cpSegmentShape *)shape;
			ChipmunkDebugDrawFatSegment(seg->ta, seg->tb, seg->r, LINE_COLOR, color);
			break;
		}
		case CP_POLY_SHAPE: {
			cpPolyShape *poly = (cpPolyShape *)shape;
			ChipmunkDebugDrawPolygon(poly->numVerts, poly->tVerts, poly->r, LINE_COLOR, color);
			break;
		}
		default: break;
	}
}

void ChipmunkDebugDrawShape(cpShape *shape)
{
	drawShape(shape, NULL);
}

void ChipmunkDebugDrawShapes(cpSpace *space)
{
	cpSpaceEachShape(space, drawShape, NULL);
}

static const GLfloat springVAR[] = {
	0.00f, 0.0f,
	0.20f, 0.0f,
	0.25f, 3.0f,
	0.30f,-6.0f,
	0.35f, 6.0f,
	0.40f,-6.0f,
	0.45f, 6.0f,
	0.50f,-6.0f,
	0.55f, 6.0f,
	0.60f,-6.0f,
	0.65f, 6.0f,
	0.70f,-3.0f,
	0.75f, 6.0f,
	0.80f, 0.0f,
	1.00f, 0.0f,
};
static const int springVAR_count = sizeof(springVAR)/sizeof(GLfloat)/2;

static void
drawSpring(cpDampedSpring *spring, cpBody *body_a, cpBody *body_b)
{
	cpVect a = cpvadd(body_a->p, cpvrotate(spring->anchr1, body_a->rot));
	cpVect b = cpvadd(body_b->p, cpvrotate(spring->anchr2, body_b->rot));
	
	ChipmunkDebugDrawDot(5, a, CONSTRAINT_COLOR);
	ChipmunkDebugDrawDot(5, b, CONSTRAINT_COLOR);

	cpVect delta = cpvsub(b, a);

	// TODO
//	glPushMatrix(); {
//		GLfloat x = a.x;
//		GLfloat y = a.y;
//		GLfloat cos = delta.x;
//		GLfloat sin = delta.y;
//		GLfloat s = 1.0f/cpvlength(delta);
//
//		const GLfloat matrix[] = {
//				 cos,    sin, 0.0f, 0.0f,
//			-sin*s,  cos*s, 0.0f, 0.0f,
//				0.0f,   0.0f, 1.0f, 0.0f,
//					 x,      y, 0.0f, 1.0f,
//		};
//		
//		glMultMatrixf(matrix);
//		glDrawArrays(GL_LINE_STRIP, 0, springVAR_count);
//	} glPopMatrix();
}

static void
drawConstraint(cpConstraint *constraint, void *unused)
{
	cpBody *body_a = constraint->a;
	cpBody *body_b = constraint->b;

	const cpConstraintClass *klass = constraint->klass;
	if(klass == cpPinJointGetClass()){
		cpPinJoint *joint = (cpPinJoint *)constraint;
	
		cpVect a = cpvadd(body_a->p, cpvrotate(joint->anchr1, body_a->rot));
		cpVect b = cpvadd(body_b->p, cpvrotate(joint->anchr2, body_b->rot));
		
		ChipmunkDebugDrawDot(5, a, CONSTRAINT_COLOR);
		ChipmunkDebugDrawDot(5, b, CONSTRAINT_COLOR);
		ChipmunkDebugDrawSegment(a, b, CONSTRAINT_COLOR);
	} else if(klass == cpSlideJointGetClass()){
		cpSlideJoint *joint = (cpSlideJoint *)constraint;
	
		cpVect a = cpvadd(body_a->p, cpvrotate(joint->anchr1, body_a->rot));
		cpVect b = cpvadd(body_b->p, cpvrotate(joint->anchr2, body_b->rot));
		
		ChipmunkDebugDrawDot(5, a, CONSTRAINT_COLOR);
		ChipmunkDebugDrawDot(5, b, CONSTRAINT_COLOR);
		ChipmunkDebugDrawSegment(a, b, CONSTRAINT_COLOR);
	} else if(klass == cpPivotJointGetClass()){
		cpPivotJoint *joint = (cpPivotJoint *)constraint;
	
		cpVect a = cpvadd(body_a->p, cpvrotate(joint->anchr1, body_a->rot));
		cpVect b = cpvadd(body_b->p, cpvrotate(joint->anchr2, body_b->rot));

		ChipmunkDebugDrawDot(5, a, CONSTRAINT_COLOR);
		ChipmunkDebugDrawDot(5, b, CONSTRAINT_COLOR);
	} else if(klass == cpGrooveJointGetClass()){
		cpGrooveJoint *joint = (cpGrooveJoint *)constraint;
	
		cpVect a = cpvadd(body_a->p, cpvrotate(joint->grv_a, body_a->rot));
		cpVect b = cpvadd(body_a->p, cpvrotate(joint->grv_b, body_a->rot));
		cpVect c = cpvadd(body_b->p, cpvrotate(joint->anchr2, body_b->rot));
		
		ChipmunkDebugDrawDot(5, c, CONSTRAINT_COLOR);
		ChipmunkDebugDrawSegment(a, b, CONSTRAINT_COLOR);
	} else if(klass == cpDampedSpringGetClass()){
		drawSpring((cpDampedSpring *)constraint, body_a, body_b);
	}
}

void ChipmunkDebugDrawConstraint(cpConstraint *constraint)
{
	drawConstraint(constraint, NULL);
}

void ChipmunkDebugDrawConstraints(cpSpace *space)
{
	cpSpaceEachConstraint(space, drawConstraint, NULL);
}

void ChipmunkDebugDrawCollisionPoints(cpSpace *space)
{
	cpArray *arbiters = space->arbiters;
	Color color = RGBAColor(1.0f, 0.0f, 0.0f, 1.0f);
	
	for(int i=0; i<arbiters->num; i++){
		cpArbiter *arb = (cpArbiter*)arbiters->arr[i];
		
		for(int j=0; j<arb->numContacts; j++){
			cpVect p = arb->contacts[j].p;
			cpVect n = arb->contacts[j].n;
			cpFloat d = 2.0 - arb->contacts[j].dist/2.0;
			
			cpVect a = cpvadd(p, cpvmult(n,  d));
			cpVect b = cpvadd(p, cpvmult(n, -d));
			ChipmunkDebugDrawSegment(a, b, color);
		}
	}
}

void ChipmunkDebugDrawFlushRenderer(void)
{
	glBindBuffer(GL_ARRAY_BUFFER, vbo);
	glBufferData(GL_ARRAY_BUFFER, sizeof(Triangle)*triangle_count, triangle_buffer, GL_STREAM_DRAW);
	
	glUseProgram(program);
	glUniform1f(glGetUniformLocation(program, "u_outline_coef"), ChipmunkDebugDrawPointLineScale);
	
	glBindVertexArrayAPPLE(vao);
	glDrawArrays(GL_TRIANGLES, 0, triangle_count*3);
		
	ChipmunkDebugDrawClearRenderer();
	CHECK_GL_ERRORS();
}

void ChipmunkDebugDrawClearRenderer(void)
{
	triangle_count = 0;
}
