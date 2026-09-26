#define MAX_FADING_VEHICLE_SHADOWS 12

// Eight live lights plus four short-lived outgoing shadows.  The extra states
// exist only for cross-fades and never register additional scene lights.

struct CFadingVehicleShadow
{
	CVector sourcePos;
	CVector sourceVelocity;
	CVector lightDir;
	float strength;
	float targetStrength;
	bool used;
	bool seen;
	bool headlight;
};

static CFadingVehicleShadow aFadingVehicleShadows[MAX_FADING_VEHICLE_SHADOWS];
static uint32 nFadingVehicleShadowUpdateTime;

static void
ResetFadingVehicleShadows(void)
{
	for(int32 i = 0; i < MAX_FADING_VEHICLE_SHADOWS; i++)
		aFadingVehicleShadows[i].used = false;
	fPlayerVehicleShadowScale = 1.0f;
	nFadingVehicleShadowUpdateTime = CTimer::GetTimeInMilliseconds();
}

static void
RenderFadingVehicleShadows(CVehicle *pCar)
{
	/* This update runs only on rendered frames.  A skipped Stereo frame still
	 * advances game time, so using one logic-frame time step here made lamp
	 * shadows move and fade in slow motion.  Accumulate the game-time interval
	 * between actual shadow updates; Flat keeps its original cadence while a
	 * skipped render catches up on the next visible frame. */
	uint32 now = CTimer::GetTimeInMilliseconds();
	float dt = nFadingVehicleShadowUpdateTime == 0 ? CTimer::GetTimeStepInSeconds() :
		(now-nFadingVehicleShadowUpdateTime)*(1.0f/1000.0f);
	nFadingVehicleShadowUpdateTime = now;
	dt = Min(dt, 0.1f);
	int32 numCurrent = 0;
	for(int32 i = 0; i < CPointLights::NumLights; i++){
		CRegisteredPointLight &light = CPointLights::aLights[i];
		if(light.type != CPointLights::LIGHT_POINT || !light.castExtraShadows ||
		   (light.red == 0.0f && light.green == 0.0f && light.blue == 0.0f))
			continue;
		CVector delta = light.coors - FindPlayerCoors();
		if(delta.Magnitude2D() < light.radius && delta.Magnitude() > 0.001f)
			numCurrent++;
	}

	for(int32 i = 0; i < MAX_FADING_VEHICLE_SHADOWS; i++){
		aFadingVehicleShadows[i].seen = false;
		aFadingVehicleShadows[i].targetStrength = 0.0f;
	}

	// Several lights split the darkness between them instead of stacking at
	// full strength into one opaque patch.
	int32 numLayers = numCurrent+1; // include the vehicle's regular ground shadow
	float targetScale = numLayers > 2 ? Max(0.5f, Sqrt(2.0f/(float)numLayers)) : 1.0f;
	float scaleBlend = Min(dt/0.25f, 1.0f);
	fPlayerVehicleShadowScale += (targetScale-fPlayerVehicleShadowScale)*scaleBlend;
	float strengthScale = fPlayerVehicleShadowScale;
	for(int32 i = 0; i < CPointLights::NumLights; i++){
		CRegisteredPointLight &light = CPointLights::aLights[i];
		if(light.type != CPointLights::LIGHT_POINT || !light.castExtraShadows ||
		   (light.red == 0.0f && light.green == 0.0f && light.blue == 0.0f))
			continue;

		CVector vecLight = light.coors - FindPlayerCoors();
		float lightDist = vecLight.Magnitude();
		float groundDist = vecLight.Magnitude2D();
		if(groundDist >= light.radius || lightDist <= 0.001f)
			continue;

		int32 slot = -1;
		float nearest = light.isVehicleHeadlight ? 16.0f : 1.0f;
		for(int32 s = 0; s < MAX_FADING_VEHICLE_SHADOWS; s++){
			CFadingVehicleShadow &shadow = aFadingVehicleShadows[s];
			if(!shadow.used || shadow.seen || shadow.headlight != light.isVehicleHeadlight)
				continue;
			float distSqr = (shadow.sourcePos-light.coors).MagnitudeSqr();
			if(distSqr < nearest){
				nearest = distSqr;
				slot = s;
			}
		}
		if(slot < 0){
			for(int32 s = 0; s < MAX_FADING_VEHICLE_SHADOWS; s++)
				if(!aFadingVehicleShadows[s].used){
					slot = s;
					break;
				}
		}
		if(slot < 0)
			continue;

		CFadingVehicleShadow &shadow = aFadingVehicleShadows[slot];
		vecLight *= 1.0f/lightDist;
		if(!shadow.used){
			shadow.used = true;
			shadow.strength = 0.0f;
			shadow.sourceVelocity = CVector(0.0f, 0.0f, 0.0f);
			shadow.lightDir = vecLight;
			shadow.headlight = light.isVehicleHeadlight;
		}else{
			if(dt > 0.001f){
				CVector measuredVelocity = (light.coors-shadow.sourcePos)*(1.0f/dt);
				float speed = measuredVelocity.Magnitude();
				if(speed > 80.0f)
					measuredVelocity *= 80.0f/speed;
				shadow.sourceVelocity = shadow.sourceVelocity*0.5f + measuredVelocity*0.5f;
			}
			float dirBlend = Min(dt/0.12f, 1.0f);
			shadow.lightDir = shadow.lightDir*(1.0f-dirBlend) + vecLight*dirBlend;
			float dirLength = shadow.lightDir.Magnitude();
			if(dirLength > 0.001f)
				shadow.lightDir *= 1.0f/dirLength;
		}
		shadow.sourcePos = light.coors;
		shadow.seen = true;

		int32 fullStrength = Max(7*CTimeCycle::GetLightShadowStrength()/8,
			3*CTimeCycle::GetShadowStrength()/4);
		float distanceStrength = groundDist < light.radius*0.5f ? 1.0f :
			1.0f-(2.0f*groundDist-light.radius)/light.radius;
		shadow.targetStrength = fullStrength*distanceStrength*strengthScale;
	}

	for(int32 i = 0; i < MAX_FADING_VEHICLE_SHADOWS; i++){
		CFadingVehicleShadow &shadow = aFadingVehicleShadows[i];
		if(!shadow.used)
			continue;
		if(!shadow.seen){
			// Keep an outgoing shadow attached to the motion that produced it.
			// Recomputing from the moving player also lets stationary lamp shadows
			// continue to sweep across the car during their short fade-out.
			shadow.sourcePos += shadow.sourceVelocity*dt;
			shadow.sourceVelocity *= Max(0.0f, 1.0f-dt/0.4f);
			CVector extrapolatedDir = shadow.sourcePos-FindPlayerCoors();
			float extrapolatedLength = extrapolatedDir.Magnitude();
			if(extrapolatedLength > 0.001f)
				shadow.lightDir = extrapolatedDir*(1.0f/extrapolatedLength);
		}
		float fadeTime = shadow.targetStrength > shadow.strength ? 0.18f : 0.32f;
		shadow.strength += (shadow.targetStrength-shadow.strength)*Min(dt/fadeTime, 1.0f);
		if(!shadow.seen && shadow.strength < 0.5f){
			shadow.used = false;
			continue;
		}
		if(shadow.strength < 0.5f)
			continue;

		float verticality = Abs(shadow.lightDir.z);
		float vehicleWidth = pCar->GetColModel()->boundingBox.GetSize().x;
		float vehicleHeight = pCar->GetColModel()->boundingBox.GetSize().y;
		float shadowLength = (vehicleHeight/2)*(1.0f+0.25f*verticality);
		float shadowWidth = vehicleWidth*(1.0f/3.0f+verticality/12.0f);
		CVector shadowPos = pCar->GetPosition();
		shadowPos.x -= shadow.lightDir.x*1.2f;
		shadowPos.y -= shadow.lightDir.y*1.2f;
		shadowPos.x -= ((vehicleHeight/2)-pCar->GetColModel()->boundingBox.max.y)*pCar->GetForward().x;
		shadowPos.y -= ((vehicleHeight/2)-pCar->GetColModel()->boundingBox.max.y)*pCar->GetForward().y;
		float sideSign = pCar->GetUp().z > 0.0f ? 1.0f : -1.0f;
		STORE_FADING_VEHICLE_SHADOW(SHADOWTYPE_DARK, gpShadowCarTex, &shadowPos,
			pCar->GetForward().x*shadowLength, pCar->GetForward().y*shadowLength,
			pCar->GetRight().x*shadowWidth*sideSign, pCar->GetRight().y*shadowWidth*sideSign,
			(int16)shadow.strength, 0, 0, 0, 4.5f, false, 1.0f);
	}
}

#undef MAX_FADING_VEHICLE_SHADOWS
