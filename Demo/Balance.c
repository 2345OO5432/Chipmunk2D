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
 
#include <stdlib.h>
#include <math.h>

#include "chipmunk.h"
#include "ChipmunkDemo.h"

#include "util.h"

static cpSpace *space;

static cpBody *balance_body;
static cpFloat balance_sin = 0.0;
//static cpFloat last_v = 0.0;

static cpBody *wheel_body;
static cpConstraint *motor;

/*
	TODO
	- Clamp max angle dynamically based on output torque.
	- Figure out the incline/stacking problem
*/


static void motor_preSolve(cpConstraint *motor, cpSpace *space)
{
	cpFloat dt = cpSpaceGetCurrentTimeStep(space);
	
	cpFloat mouse = ChipmunkDemoMouse.x/320.0;
	cpFloat key = ChipmunkDemoKeyboard.x;
	
	cpFloat torque = cpConstraintGetImpulse(motor)/cpSpaceGetCurrentTimeStep(space);
	cpFloat max_torque = cpConstraintGetMaxForce(motor);
	ChipmunkDemoPrintString("torque: %3.0f%% ", 100.0*torque/max_torque);
	
//	cpFloat v = balance_body->v.x;
//	ChipmunkDemoPrintString("a/sin: %5.2f\n", (v - last_v)/dt/(balance_body->a));
//	last_v = v;
	
//	cpFloat target_x = 200.0*cpfsin(ChipmunkDemoTime/2.0);
	cpFloat target_x = ChipmunkDemoMouse.x;
	ChipmunkDebugDrawSegment(cpv(target_x, -1000.0), cpv(target_x, 1000.0), RGBAColor(1.0, 0.0, 0.0, 1.0));
	
	cpFloat target_v = bias_coef(0.5, dt/1.2)*(target_x - balance_body->p.x)/dt;
	cpFloat error_v = (target_v - balance_body->v.x);
	cpFloat target_sin = 3.0e-3*bias_coef(0.1, dt)*error_v/dt;
	ChipmunkDemoPrintString("v: %5.2f target_v: %5.2f\n", balance_body->v.x, target_v);
	
	cpFloat max_balance = 0.6;
	balance_sin = cpfclamp(balance_sin - 6.0e-5*bias_coef(0.2, dt)*error_v/dt, -max_balance, max_balance);
	
	cpFloat max_sin = cpfsin(0.6);
	cpFloat target_a = asin(cpfclamp(-target_sin + balance_sin, -max_sin, max_sin));
	cpFloat angular_diff = asin(cpvcross(balance_body->rot, cpvforangle(target_a)));
	cpFloat target_w = bias_coef(0.1, dt/0.4)*(angular_diff)/dt;
	ChipmunkDemoPrintString("a: %.7f target_a: %.7f balance_sin: %.7f\n", balance_body->a, target_a, balance_sin);
	
	cpFloat max_rate = 50.0;
	cpFloat rate = cpfclamp(wheel_body->w + balance_body->w - target_w, -max_rate, max_rate);
	cpSimpleMotorSetRate(motor, cpfclamp(rate, -max_rate, max_rate));
	cpConstraintSetMaxForce(motor, 8.0e4);
}


static void
update(int ticks)
{
	int steps = 1;
	cpFloat dt = 1.0f/60.0f/(cpFloat)steps;
	
	for(int i=0; i<steps; i++){
		cpSpaceStep(space, dt);
	}
}

static cpSpace *
init(void)
{
//	ChipmunkDemoMessageString = "Control the crane by moving the mouse. Right click to release.";
	
	space = cpSpaceNew();
	cpSpaceSetIterations(space, 30);
	cpSpaceSetGravity(space, cpv(0, -500));
	
	{
		cpShape *shape = NULL;
		cpBody *staticBody = space->staticBody;
		
//		shape = cpSpaceAddShape(space, cpSegmentShapeNew(staticBody, cpv(-320,-240), cpv(-320,240), 0.0f));
//		cpShapeSetElasticity(shape, 1.0f);
//		cpShapeSetFriction(shape, 0.0f);
//		cpShapeSetLayers(shape, NOT_GRABABLE_MASK);
//
//		shape = cpSpaceAddShape(space, cpSegmentShapeNew(staticBody, cpv(320,-240), cpv(320,240), 0.0f));
//		cpShapeSetElasticity(shape, 1.0f);
//		cpShapeSetFriction(shape, 0.0f);
//		cpShapeSetLayers(shape, NOT_GRABABLE_MASK);

		shape = cpSpaceAddShape(space, cpSegmentShapeNew(staticBody, cpv(-3200,-240), cpv(3200,-240), 0.0f));
		cpShapeSetElasticity(shape, 1.0f);
		cpShapeSetFriction(shape, 1.0f);
		cpShapeSetLayers(shape, NOT_GRABABLE_MASK);

		shape = cpSpaceAddShape(space, cpSegmentShapeNew(staticBody, cpv(0,-240), cpv(320,-200), 0.0f));
		cpShapeSetElasticity(shape, 1.0f);
		cpShapeSetFriction(shape, 1.0f);
		cpShapeSetLayers(shape, NOT_GRABABLE_MASK);

		shape = cpSpaceAddShape(space, cpSegmentShapeNew(staticBody, cpv(160,-240), cpv(320,-160), 0.0f));
		cpShapeSetElasticity(shape, 1.0f);
		cpShapeSetFriction(shape, 1.0f);
		cpShapeSetLayers(shape, NOT_GRABABLE_MASK);
	}
	
	
	{
		cpFloat radius = 20.0;
		cpFloat mass = 1.0;
		
		cpFloat moment = cpMomentForCircle(mass, 0.0, radius, cpvzero);
		wheel_body = cpSpaceAddBody(space, cpBodyNew(mass, moment));
		wheel_body->p = cpv(0.0, -160.0 + radius);
		
		cpShape *shape = cpSpaceAddShape(space, cpCircleShapeNew(wheel_body, radius, cpvzero));
		shape->u = 0.7;
		shape->group = 1;
	}
	
	{
		cpFloat cog_offset = 30.0;
		
		cpBB bb1 = cpBBNew(-5.0, 0.0 - cog_offset, 5.0, 40.0 - cog_offset);
		cpBB bb2 = cpBBNew(-25.0, bb1.t, 25.0, bb1.t + 10.0);
		
		cpFloat mass = 10.0;
		cpFloat moment = cpMomentForBox2(mass, bb1) + cpMomentForBox2(mass, bb2);
		
		balance_body = cpSpaceAddBody(space, cpBodyNew(mass, moment));
		balance_body->p = cpv(0.0, wheel_body->p.y + cog_offset);
		
		cpShape *shape = NULL;
		
		shape = cpSpaceAddShape(space, cpBoxShapeNew2(balance_body, bb1));
		shape->u = 1.0;
		shape->group = 1;
		
		shape = cpSpaceAddShape(space, cpBoxShapeNew2(balance_body, bb2));
		shape->u = 1.0;
		shape->group = 1;
	}
	
	cpVect anchr1 = cpBodyWorld2Local(balance_body, wheel_body->p);
	cpVect groove_a = cpvadd(anchr1, cpv(0.0,  30.0));
	cpVect groove_b = cpvadd(anchr1, cpv(0.0, -10.0));
	cpSpaceAddConstraint(space, cpGrooveJointNew(balance_body, wheel_body, groove_a, groove_b, cpvzero));
	cpSpaceAddConstraint(space, cpDampedSpringNew(balance_body, wheel_body, anchr1, cpvzero, 0.0, 6.0e2, 30.0));
	
	motor = cpSpaceAddConstraint(space, cpSimpleMotorNew(wheel_body, balance_body, 0.0));
	motor->preSolve = motor_preSolve;
	
	{
		cpFloat size = 20.0;
		cpFloat mass = 3.0;
		
		cpBody *boxBody = cpSpaceAddBody(space, cpBodyNew(mass, cpMomentForBox(mass, size, size)));
		cpBodySetPos(boxBody, cpv(200, -100));
		
		cpShape *shape = cpSpaceAddShape(space, cpBoxShapeNew(boxBody, 50, 50));
		cpShapeSetFriction(shape, 0.7);
	}
	
	return space;
}

static void
destroy(void)
{
	ChipmunkDemoFreeSpaceChildren(space);
	cpSpaceFree(space);
}

ChipmunkDemo Balance = {
	"Balance",
	init,
	update,
	ChipmunkDemoDefaultDrawImpl,
	destroy,
};
