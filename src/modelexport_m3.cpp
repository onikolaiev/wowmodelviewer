#include "globalvars.h"
#include "modelexport.h"
#include "modelexport_m3.h"
#include "modelcanvas.h"

#define	ROOT_BONE

static std::vector<ReferenceEntry> reList;

void padding(wxFFile *f, int pads=16)
{
	char pad=0xAA;
	if (f->Tell()%pads != 0) {
		int j=pads-(f->Tell()%pads);
		for(int i=0; i<j; i++) {
			f->Write(&pad, sizeof(pad));
		}
	}
}

void RefEntry(const char *id, uint32 offset, uint32 nEntries, uint32 vers)
{
	ReferenceEntry re;
	strncpy(re.id, id, 4);
	re.offset = offset;
	re.nEntries = nEntries;
	re.vers = vers;
	reList.push_back(re);
}

uint32 nSkinnedBones(Model *m, MPQFile *mpqf, uint32 start, uint32 num)
{
	ModelVertex *verts = (ModelVertex*)(mpqf->getBuffer() + m->header.ofsVertices);
	ModelBoneDef *mb = (ModelBoneDef*)(mpqf->getBuffer() + m->header.ofsBones);
	uint8 *skinned = new uint8[m->header.nBones];
	memset(skinned, 0, sizeof(uint8)*m->header.nBones);
	uint32 nSkinnedBones = 0;
	for(uint32 i=start; i<(start+num); i++) {
		for(uint32 j=0; j<4; j++) {
			if (verts[i].weights[j] != 0)
				skinned[verts[i].bones[j]] = 1;
		}
	}
	for(uint32 i=0; i<m->header.nBones; i++) {
		if (skinned[i] == 1)
		{
			uint32 j = i;
			while(1)
			{
				if(mb[j].parent > -1)
				{
					j = mb[j].parent;
					skinned[j] = 1;
				}
				else
					break;
			}
		}
	}
	for(uint32 i=0; i<m->header.nBones; i++) {
		if (skinned[i] == 1)
			nSkinnedBones ++;
	}
	wxDELETEA(skinned);
	nSkinnedBones++;
	return nSkinnedBones;
}

void ExportM2toM3(Model *m, const char *fn, bool init)
{
	if (!m)
		return;

	wxFFile f(wxString(fn, wxConvUTF8), wxT("w+b"));
	reList.clear();

	if (!f.IsOpened()) {
		wxLogMessage(_T("Error: Unable to open file '%s'. Could not export model."), fn);
		return;
	}
	LogExportData(_T("M3"),wxString(fn, wxConvUTF8).BeforeLast(SLASH),_T("M2"));

	MPQFile mpqf((char *)m->modelname.c_str());
	MPQFile mpqfv((char *)m->lodname.c_str());

	// 1. FileHead
	RefEntry("43DM", f.Tell(), 1, 0xB);

	struct MD34 fHead;
	memset(&fHead, 0, sizeof(fHead));
	strcpy(fHead.id, "43DM");
	fHead.mref.nEntries = 1;
	fHead.mref.ref = ++fHead.nRefs;
	memset(&fHead.padding, 0xAA, sizeof(fHead.padding));
	f.Seek(sizeof(fHead), wxFromCurrent);

	// 2. ModelHead
	RefEntry("LDOM", f.Tell(), 1, 0x17);

	struct MODL mdata;
	memset(&mdata, 0, sizeof(mdata));
	f.Seek(sizeof(mdata), wxFromCurrent);

	// 3. Content
	// Modelname
	wxString n(fn, wxConvUTF8);
	n = n.AfterLast('\\').BeforeLast('.');
	n.Append(_T(".max"));

	mdata.name.nEntries = n.Len()+1;
	mdata.name.ref = ++fHead.nRefs;
	RefEntry("RAHC", f.Tell(), mdata.name.nEntries, 0);
	f.Write(n.c_str(), n.Len()+1);
	padding(&f);

	mdata.type = 0x180d53;
	
	// mSEQS
	uint32 nAnimations = 0;
	uint32 *logAnimations = new uint32[m->header.nAnimations];
	uint32 *reAnimations = new uint32[m->header.nAnimations];
	wxString *nameAnimations = new wxString[m->header.nAnimations];
	int Walks = 0, Stands =0, Attacks = 0, Deaths = 0;
	for(uint32 i=0; i<m->header.nAnimations; i++) {
		wxString strName;
		try {
			AnimDB::Record rec = animdb.getByAnimID(m->anims[i].animID);
			strName = rec.getString(AnimDB::Name);
		} catch (AnimDB::NotFound) {
			strName = _T("???");
		}
		if (!strName.StartsWith(_T("Run")) && !strName.StartsWith(_T("Stand")) && 
				!strName.StartsWith(_T("Attack")) && !strName.StartsWith(_T("Death")))
			continue;
		if (strName.StartsWith(_T("StandWound")))
			continue;
		//if (strName != _T("Run"))
		//	continue;
		nameAnimations[i] = strName;
		if (strName.StartsWith(_T("Run"))) {
			if (Walks == 0)
				nameAnimations[nAnimations] = _T("Walk");
			else
				nameAnimations[nAnimations] = wxString::Format(_T("Walk %02d"), Walks);
			Walks ++;
		}

		if (strName.StartsWith(_T("Stand"))) {
			if (Stands == 0)
				nameAnimations[nAnimations] = _T("Stand");
			else
				nameAnimations[nAnimations] = wxString::Format(_T("Stand %02d"), Stands);
			Stands ++;
		}
		if (strName.StartsWith(_T("Attack"))) {
			if (Attacks == 0)
				nameAnimations[nAnimations] = _T("Attack");
			else
				nameAnimations[nAnimations] = wxString::Format(_T("Attack %02d"), Attacks);
			Attacks ++;
		}
		if (strName.StartsWith(_T("Death"))) {
			if (Deaths == 0)
				nameAnimations[nAnimations] = _T("Death");
			else
				nameAnimations[nAnimations] = wxString::Format(_T("Death %02d"), Deaths);
			Deaths ++;
		}

		logAnimations[nAnimations] = i;
		reAnimations[i] = nAnimations; 
		nAnimations++;
		//break;
	}
	int chunk_offset, datachunk_offset;

	RefEntry("SQES", f.Tell(), nAnimations, 1);
	chunk_offset = f.Tell();
	mdata.mSEQS.nEntries = nAnimations;
	mdata.mSEQS.ref = ++fHead.nRefs;
	SEQS *seqs = new SEQS[nAnimations];
	memset(seqs, 0, sizeof(SEQS)*nAnimations);
	f.Seek(sizeof(SEQS)*nAnimations, wxFromCurrent);
	padding(&f);
	for(uint32 i=0; i<nAnimations; i++) {
		wxString strName = nameAnimations[i];
		seqs[i].name.nEntries = strName.Len()+1;
		seqs[i].name.ref = ++fHead.nRefs;
		RefEntry("RAHC", f.Tell(), seqs[i].name.nEntries, 0);
		f.Write(strName.c_str(), strName.Len()+1);
		padding(&f);
	}
	datachunk_offset = f.Tell();
	f.Seek(chunk_offset, wxFromStart);
	for(uint32 i=0; i<nAnimations; i++) {
		int j = logAnimations[i];
		seqs[i].d1[0] = -1;
		seqs[i].d1[1] = -1;
		seqs[i].length = m->anims[j].timeEnd;
		seqs[i].moveSpeed = m->anims[j].moveSpeed;
		seqs[i].frequency = m->anims[j].playSpeed; // ?
		seqs[i].ReplayStart = 1;
		seqs[i].ReplayEnd = 1;
		seqs[i].d4[0] = 0x64;
		seqs[i].boundSphere.min = m->header.ps.VertexBox[0];
		seqs[i].boundSphere.max = m->header.ps.VertexBox[1];
		seqs[i].boundSphere.radius = m->header.ps.VertexRadius;
		f.Write(&seqs[i], sizeof(SEQS));
	}
	
	f.Seek(datachunk_offset, wxFromStart);

	// mSTC
	RefEntry("_CTS", f.Tell(), nAnimations, 4);
	chunk_offset = f.Tell();
	mdata.mSTC.nEntries = nAnimations;
	mdata.mSTC.ref = ++fHead.nRefs;
	STC *stcs = new STC[mdata.mSTC.nEntries];
	memset(stcs, 0, sizeof(STC)*mdata.mSTC.nEntries);
	f.Seek(sizeof(STC)*mdata.mSTC.nEntries, wxFromCurrent);
	padding(&f);
	for(uint32 i=0; i<mdata.mSTC.nEntries; i++) {
		int anim_offset = logAnimations[i];

		// name
		wxString strName = nameAnimations[i];
		strName.Append(_T("_full"));
		stcs[i].name.nEntries = strName.Len()+1;
		stcs[i].name.ref = ++fHead.nRefs;
		RefEntry("RAHC", f.Tell(), stcs[i].name.nEntries, 0);
		f.Write(strName.c_str(), strName.Len()+1);
		padding(&f);

		// animid
		for(int j=0; j<m->header.nBones; j++) {
			if (m->bones[j].trans.uses(anim_offset)  && m->bones[j].trans.data[anim_offset].size() > 0) {
				stcs[i].arVec3D.nEntries++;
			}
			if (m->bones[j].scale.uses(anim_offset)  && m->bones[j].scale.data[anim_offset].size() > 0) {
				stcs[i].arVec3D.nEntries++;
			}
			if (m->bones[j].rot.uses(anim_offset)  && m->bones[j].rot.data[anim_offset].size() > 0) {
				stcs[i].arQuat.nEntries++;
			}
		}
		stcs[i].animid.nEntries = stcs[i].arVec3D.nEntries + stcs[i].arQuat.nEntries;
		if (stcs[i].animid.nEntries > 0) {
			stcs[i].animid.ref = ++fHead.nRefs;
			RefEntry("_23U", f.Tell(), stcs[i].animid.nEntries, 0);
			for(int j=0; j<m->header.nBones; j++) {
				if (m->bones[j].trans.uses(anim_offset) && m->bones[j].trans.data[anim_offset].size() > 0) {
					int16 p = 2;
#ifdef	ROOT_BONE
					int16 a = j + 1;
#else
					int16 a = j;
#endif
					f.Write(&a, sizeof(int16));
					f.Write(&p, sizeof(int16));
				}
				if (m->bones[j].scale.uses(anim_offset) && m->bones[j].scale.data[anim_offset].size() > 0) {
					int16 p = 5;
#ifdef	ROOT_BONE
					int16 a = j + 1;
#else
					int16 a = j;
#endif
					f.Write(&a, sizeof(int16));
					f.Write(&p, sizeof(int16));
				}
				if (m->bones[j].rot.uses(anim_offset) && m->bones[j].rot.data[anim_offset].size() > 0) {
					int16 p = 3;
#ifdef	ROOT_BONE
					int16 a = j + 1;
#else
					int16 a = j;
#endif
					f.Write(&a, sizeof(int16));
					f.Write(&p, sizeof(int16));
				}
			}
			padding(&f);
		}

		// animindex
		stcs[i].animindex.nEntries = stcs[i].animid.nEntries;
		if (stcs[i].animindex.nEntries > 0){
			stcs[i].animindex.ref = ++fHead.nRefs;
			RefEntry("_23U", f.Tell(), stcs[i].animindex.nEntries, 0);
			int16 tcount = 0;
			int16 rcount = 0;
			for(int j=0; j<m->header.nBones; j++) {
				if (m->bones[j].trans.uses(anim_offset) && m->bones[j].trans.data[anim_offset].size() > 0) {
					int16 p = 2;
					f.Write(&tcount, sizeof(int16));
					f.Write(&p, sizeof(int16));
					tcount++;
				}
				if (m->bones[j].scale.uses(anim_offset) && m->bones[j].scale.data[anim_offset].size() > 0) {
					int16 p = 2;
					f.Write(&tcount, sizeof(int16));
					f.Write(&p, sizeof(int16));
					tcount++;
				}
				if (m->bones[j].rot.uses(anim_offset)  && m->bones[j].rot.data[anim_offset].size() > 0) {
					int16 p = 3;
					f.Write(&rcount, sizeof(int16));
					f.Write(&p, sizeof(int16));
					rcount++;
				}
			}
			padding(&f);
		}


		SD *sds;
		int ii;
		int chunk_offset2, datachunk_offset2;

		// Events, VEDS
		stcs[i].Events.nEntries = 1;
		stcs[i].Events.ref = ++fHead.nRefs;
		RefEntry("VEDS", f.Tell(), stcs[i].Events.nEntries, 0);
		chunk_offset2 = f.Tell();
		SD sd;
		memset(&sd, 0, sizeof(sd));
		f.Seek(sizeof(sd), wxFromCurrent);
		for(int j=0; j<m->header.nBones; j++) {
			if (m->bones[j].trans.uses(anim_offset) && m->bones[j].trans.data[anim_offset].size() > 0) {
				//int counts = m->bones[j].trans.data[anim_offset].size();
				sd.length = seqs[reAnimations[anim_offset]].length;  //m->bones[j].trans.times[anim_offset][counts-1];
				break;
			}
		}
		sd.timeline.nEntries = 1;
		sd.timeline.ref = ++fHead.nRefs;
		sd.flags = 1;
		RefEntry("_23I", f.Tell(), sd.timeline.nEntries, 0);
		f.Write(&sd.length, sizeof(int32));
		padding(&f);
		sd.data.nEntries = 1;
		sd.data.ref = ++fHead.nRefs;
		RefEntry("TNVE", f.Tell(), sd.data.nEntries, 0);
		EVNT evnt;
		// name
		strName = _T("Evt_SeqEnd");
		memset(&evnt, 0, sizeof(evnt));
		evnt.name.nEntries = strName.Len()+1;
		evnt.name.ref = ++fHead.nRefs;
		evnt.d1 = -1;
		evnt.s1 = -1;
		evnt.a = Vec4D(1.0f, 0.0f, 0.0f, 0.0f);
		evnt.b = Vec4D(0.0f, 1.0f, 0.0f, 0.0f);
		evnt.c = Vec4D(0.0f, 0.0f, 1.0f, 0.0f);
		evnt.d = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		evnt.d2[0] = 4;
		
		f.Write(&evnt, sizeof(evnt));
		padding(&f);
		RefEntry("RAHC", f.Tell(), evnt.name.nEntries, 0);
		f.Write(strName.c_str(), strName.Len()+1);
		padding(&f);
		datachunk_offset2 = f.Tell();
		f.Seek(chunk_offset2, wxFromStart);
		f.Write(&sd, sizeof(sd));
		f.Seek(datachunk_offset2, wxFromStart);

		// Trans and Scale, V3DS
		if (stcs[i].arVec3D.nEntries > 0) {
			stcs[i].arVec3D.ref = ++fHead.nRefs;
			RefEntry("V3DS", f.Tell(), stcs[i].arVec3D.nEntries, 0);
			chunk_offset2 = f.Tell();
			sds = new SD[stcs[i].arVec3D.nEntries];
			memset(sds, 0, sizeof(SD)*stcs[i].arVec3D.nEntries);
			f.Seek(sizeof(sd)*stcs[i].arVec3D.nEntries, wxFromCurrent);
			ii=0;
			for(int j=0; j<m->header.nBones; j++) {
				// trans
				if (m->bones[j].trans.uses(anim_offset) && m->bones[j].trans.data[anim_offset].size() > 0) {
					int counts = m->bones[j].trans.data[anim_offset].size();
					sds[ii].timeline.nEntries = counts;
					sds[ii].timeline.ref = ++fHead.nRefs;
					RefEntry("_23I", f.Tell(), sds[ii].timeline.nEntries, 0);
					for (int k=0; k<counts; k++) {
						f.Write(&m->bones[j].trans.times[anim_offset][k], sizeof(int32));
					}
					padding(&f);
					sds[ii].length = seqs[reAnimations[anim_offset]].length; //m->bones[j].trans.times[anim_offset][counts-1];
					sds[ii].data.nEntries = counts;
					sds[ii].data.ref = ++fHead.nRefs;
					RefEntry("3CEV", f.Tell(), sds[ii].data.nEntries, 0);

					for (int k=0; k<counts; k++) {
						Vec3D tran;
						if (m->bones[j].parent > -1)
							tran = m->bones[j].pivot - m->bones[m->bones[j].parent].pivot;
						else
							tran = m->bones[j].pivot;
						tran += m->bones[j].trans.data[anim_offset][k];
						tran.z *= -1.0f;

						f.Write(&tran.x, sizeof(int32));
						f.Write(&tran.z, sizeof(int32));
						f.Write(&tran.y, sizeof(int32));
					}
					padding(&f);
					ii++;
				}
				//scale
				if (m->bones[j].scale.uses(anim_offset) && m->bones[j].scale.data[anim_offset].size() > 0) {
					int counts = m->bones[j].scale.data[anim_offset].size();
					sds[ii].timeline.nEntries = counts;
					sds[ii].timeline.ref = ++fHead.nRefs;
					RefEntry("_23I", f.Tell(), sds[ii].timeline.nEntries, 0);
					for (int k=0; k<counts; k++) {
						f.Write(&m->bones[j].scale.times[anim_offset][k], sizeof(int32));
					}
					padding(&f);
					sds[ii].length = seqs[reAnimations[anim_offset]].length; //m->bones[j].scale.times[anim_offset][counts-1];
					sds[ii].data.nEntries = counts;
					sds[ii].data.ref = ++fHead.nRefs;
					RefEntry("3CEV", f.Tell(), sds[ii].data.nEntries, 0);

					for (int k=0; k<counts; k++) {
						Vec3D scale;
						scale = m->bones[j].scale.data[anim_offset][k];
						

						f.Write(&scale.x, sizeof(int32));
						f.Write(&scale.z, sizeof(int32));
						f.Write(&scale.y, sizeof(int32));
					}
					padding(&f);
					ii++;
				}
			}
			datachunk_offset2 = f.Tell();
			f.Seek(chunk_offset2, wxFromStart);
			for(int j=0; j<stcs[i].arVec3D.nEntries; j++) {
				f.Write(&sds[j], sizeof(sd));
			}
			wxDELETEA(sds);
			f.Seek(datachunk_offset2, wxFromStart);
		}

		// Rot, Q4DS
		if (stcs[i].arQuat.nEntries > 0) {
			stcs[i].arQuat.ref = ++fHead.nRefs;
			RefEntry("Q4DS", f.Tell(), stcs[i].arQuat.nEntries, 0);
			chunk_offset2 = f.Tell();
			sds = new SD[stcs[i].arQuat.nEntries];
			memset(sds, 0, sizeof(SD)*stcs[i].arQuat.nEntries);
			f.Seek(sizeof(sd)*stcs[i].arQuat.nEntries, wxFromCurrent);
			ii=0;
			for(int j=0; j<m->header.nBones; j++) {
				if (m->bones[j].rot.uses(anim_offset) && m->bones[j].rot.data[anim_offset].size() > 0) {
					int counts = m->bones[j].rot.data[anim_offset].size();
					sds[ii].timeline.nEntries = counts;
					sds[ii].timeline.ref = ++fHead.nRefs;
					RefEntry("_23I", f.Tell(), sds[ii].timeline.nEntries, 0);
					for (int k=0; k<counts; k++) {
						f.Write(&m->bones[j].rot.times[anim_offset][k], sizeof(int32));
					}
					padding(&f);
					sds[ii].length = seqs[reAnimations[anim_offset]].length; //m->bones[j].rot.times[anim_offset][counts-1];
					sds[ii].data.nEntries = counts;
					sds[ii].data.ref = ++fHead.nRefs;
					RefEntry("TAUQ", f.Tell(), sds[ii].data.nEntries, 0);
					for (int k=0; k<counts; k++) {
						Vec4D rot;
						rot.x = -m->bones[j].rot.data[anim_offset][k].x;
						rot.y = m->bones[j].rot.data[anim_offset][k].z;
						rot.z = -m->bones[j].rot.data[anim_offset][k].y;
						rot.w = m->bones[j].rot.data[anim_offset][k].w;

						f.Write(&rot, sizeof(rot));
					}
					padding(&f);
					ii++;
				}
			}
			datachunk_offset2 = f.Tell();
			f.Seek(chunk_offset2, wxFromStart);
			for(int j=0; j<stcs[i].arQuat.nEntries; j++) {
				f.Write(&sds[j], sizeof(sd));
			}
			wxDELETEA(sds);
			f.Seek(datachunk_offset2, wxFromStart);
		}

	}
	datachunk_offset = f.Tell();
	f.Seek(chunk_offset, wxFromStart);
	for(uint32 i=0; i<mdata.mSTC.nEntries; i++) {
		stcs[i].indSEQ[0] = stcs[i].indSEQ[1] = i;
		f.Write(&stcs[i], sizeof(STC));
	}
	f.Seek(datachunk_offset, wxFromStart);

	// mSTG
	RefEntry("_GTS", f.Tell(), nAnimations, 0);
	chunk_offset = f.Tell();
	mdata.mSTG.nEntries = nAnimations;
	mdata.mSTG.ref = ++fHead.nRefs;
	STG *stgs = new STG[mdata.mSTG.nEntries];
	memset(stgs, 0, sizeof(STG)*mdata.mSTG.nEntries);
	f.Seek(sizeof(STG)*mdata.mSTG.nEntries, wxFromCurrent);
	padding(&f);
	for(uint32 i=0; i<mdata.mSTG.nEntries; i++) {
		// name
		wxString strName = nameAnimations[i];
		stgs[i].name.nEntries = strName.Len()+1;
		stgs[i].name.ref = ++fHead.nRefs;
		RefEntry("RAHC", f.Tell(), stgs[i].name.nEntries, 0);
		f.Write(strName.c_str(), strName.Len()+1);
		padding(&f);
		
		// stcID
		stgs[i].stcID.nEntries = 1;
		stgs[i].stcID.ref = ++fHead.nRefs;
		RefEntry("_23U", f.Tell(), stgs[i].stcID.nEntries, 0);
		f.Write(&i, sizeof(uint32));
		padding(&f);
	}
	datachunk_offset = f.Tell();
	f.Seek(chunk_offset, wxFromStart);
	for(uint32 i=0; i<mdata.mSTG.nEntries; i++) {
		f.Write(&stgs[i], sizeof(STG));
	}
	wxDELETEA(stgs);
	f.Seek(datachunk_offset, wxFromStart);

	// mSTS
	mdata.mSTS.nEntries = nAnimations;
	mdata.mSTS.ref = ++fHead.nRefs;
	RefEntry("_STS", f.Tell(), mdata.mSTS.nEntries, 0);
	chunk_offset = f.Tell();
	STS *stss = new STS[mdata.mSTS.nEntries];
	memset(stss, 0, sizeof(STS)*mdata.mSTS.nEntries);
	f.Seek(sizeof(STS)*mdata.mSTS.nEntries, wxFromCurrent);
	padding(&f);
	for(uint32 i=0; i<mdata.mSTS.nEntries; i++) {
		int anim_offset = logAnimations[i];

		stss[i].animid.nEntries = stcs[i].animid.nEntries;
		if (stss[i].animid.nEntries) {
			stss[i].animid.ref = ++fHead.nRefs;
			RefEntry("_23U", f.Tell(), stss[i].animid.nEntries, 0);
			for(int j=0; j<m->header.nBones; j++) {
				if (m->bones[j].trans.uses(anim_offset)  && m->bones[j].trans.data[anim_offset].size() > 0) {
					int16 p = 2;
#ifdef	ROOT_BONE
					int16 a = j + 1;
#else
					int16 a = j;
#endif
					f.Write(&a, sizeof(int16));
					f.Write(&p, sizeof(int16));
				}
				if (m->bones[j].scale.uses(anim_offset)  && m->bones[j].scale.data[anim_offset].size() > 0) {
					int16 p = 5;
#ifdef	ROOT_BONE
					int16 a = j + 1;
#else
					int16 a = j;
#endif
					f.Write(&a, sizeof(int16));
					f.Write(&p, sizeof(int16));
				}
				if (m->bones[j].rot.uses(anim_offset)  && m->bones[j].rot.data[anim_offset].size() > 0) {
					int16 p = 3;
#ifdef	ROOT_BONE
					int16 a = j + 1;
#else
					int16 a = j;
#endif
					f.Write(&a, sizeof(int16));
					f.Write(&p, sizeof(int16));
				}
			}
			padding(&f);
		}
	}
	datachunk_offset = f.Tell();
	f.Seek(chunk_offset, wxFromStart);
	for(uint32 i=0; i<mdata.mSTS.nEntries; i++) {
		stss[i].d1[0] = -1;
		stss[i].d1[1] = -1;
		stss[i].d1[2] = -1;
		stss[i].s1 = -1;
		f.Write(&stss[i], sizeof(STS));
	}
	wxDELETEA(stss);
	f.Seek(datachunk_offset, wxFromStart);

	// mBone
	std::vector<ModelBoneDef> boneList;
	ModelBoneDef *mb = (ModelBoneDef*)(mpqf.getBuffer() + m->header.ofsBones);

#ifdef	ROOT_BONE
	ModelBoneDef rootBone;
	memset(&rootBone, 0, sizeof(rootBone));
	rootBone.parent = -2;
	boneList.push_back(rootBone);
#endif
	for(uint32 i=0; i<m->header.nBones; i++) {
		boneList.push_back(mb[i]);
	}
#ifdef	ROOT_BONE
	ModelBoneDef animBone;
	memset(&animBone, 0, sizeof(animBone));
	animBone.parent = -2;
	boneList.push_back(animBone);
#endif

	mdata.mBone.nEntries = boneList.size();
	mdata.mBone.ref = ++fHead.nRefs;
	RefEntry("ENOB", f.Tell(), mdata.mBone.nEntries, 1);
	chunk_offset = f.Tell();
	BONE *bones = new BONE[mdata.mBone.nEntries];
	memset(bones, 0, sizeof(BONE)*mdata.mBone.nEntries);
	f.Seek(sizeof(BONE)*mdata.mBone.nEntries, wxFromCurrent);
	padding(&f);
	for(uint32 i=0; i<mdata.mBone.nEntries; i++) {
		// name
		wxString strName;
#ifdef	ROOT_BONE
		if (i == 0)
			strName = _T("Bone_Root");
		else if (i == mdata.mBone.nEntries - 1)
			strName =_T("_M3_Anim_Data");
		else
			strName = wxString::Format(_T("Bone_%d"), i-1);
#else
		strName = wxString::Format(_T("Bone_%d"), i);
#endif
		bones[i].name.nEntries = strName.Len()+1;
		bones[i].name.ref = ++fHead.nRefs;
		RefEntry("RAHC", f.Tell(), bones[i].name.nEntries, 0);
		f.Write(strName.c_str(), strName.Len()+1);
		padding(&f);
	}
	datachunk_offset = f.Tell();
	f.Seek(chunk_offset, wxFromStart);
	for(uint32 i=0; i<mdata.mBone.nEntries; i++) {
		
		bones[i].d1 = -1;
		bones[i].flags = 0xA00;
#ifdef	ROOT_BONE
		bones[i].parent = boneList[i].parent + 1;
#else
		bones[i].parent = boneList[i].parent;
#endif
		//if (bones[i].parent > 0)
		//	mdata.nSkinnedBones ++;
#if 0
		for(uint32 j=0; j<mdata.mSTC.nEntries; j++) {
			int anim_offset = logAnimations[j];
			if (m->bones[i].trans.uses(anim_offset)) {
				bones[i].initTrans.AnimRef.animid = i | (2 << 16);
				break;
			}
		}
#endif
		bones[i].initTrans.AnimRef.animid = i | (2 << 16);
		if (i > 0 && i < mdata.mBone.nEntries - 1)
		{
			for (uint32 j=0; j<mdata.mSTS.nEntries; j++)
			{
				int anim_offset = logAnimations[j];
				if (m->bones[i-1].trans.uses(anim_offset))
				{	
					bones[i].initTrans.AnimRef.flags = 1;
					bones[i].initTrans.AnimRef.animflag = 6;	
				}
			}
		}
		bones[i].initTrans.value = boneList[i].pivot;
		if (bones[i].parent > -1) {
			bones[i].initTrans.value -= boneList[bones[i].parent].pivot ;
		}
#if 0
		for(uint32 j=0; j<mdata.mSTC.nEntries; j++) {
			int anim_offset = logAnimations[j];
			if (m->bones[i].rot.uses(anim_offset)) {
				bones[i].initRot.AnimRef.animid = i | (3 << 16);
				break;
			}
		}
#endif
#ifdef	ROOT_BONE
		if (i == 0)
		{
			bones[i].initRot.AnimRef.animid = i | (3 << 16);
			bones[i].initRot.value = Vec4D(0.0f, 0.0f, -sqrt(0.5f), sqrt(0.5f));
			bones[i].initRot.unValue = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
		}
		else
		{
#endif
			bones[i].initRot.AnimRef.animid = i | (3 << 16);
			bones[i].initRot.value = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
			bones[i].initRot.unValue = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);
			if (i > 0 && i < mdata.mBone.nEntries - 1)
			{
				for (uint32 j=0; j<mdata.mSTS.nEntries; j++)
				{
					int anim_offset = logAnimations[j];
					if (m->bones[i-1].rot.uses(anim_offset))
					{	
						bones[i].initRot.AnimRef.flags = 1;
						bones[i].initRot.AnimRef.animflag = 6;	
					}
				}
			}
#ifdef	ROOT_BONE
		}
#endif
#ifdef	ROOT_BONE
		if (i == 0)
		{
			bones[i].initScale.AnimRef.animid = i | (5 << 16);
			bones[i].initScale.value = Vec3D(1.0f, 1.0f, 1.0f);
			bones[i].initScale.unValue = Vec3D(1.0f, 1.0f, 1.0f);
		}
		else
		{
#endif
			bones[i].initScale.AnimRef.animid = i | (5 << 16);
			bones[i].initScale.value = Vec3D(1.0f, 1.0f, 1.0f);
			bones[i].initScale.unValue = Vec3D(1.0f, 1.0f, 1.0f);
			if (i > 0 && i < mdata.mBone.nEntries - 1)
			{
				for (uint32 j=0; j<mdata.mSTS.nEntries; j++)
				{
					int anim_offset = logAnimations[j];
					if (m->bones[i-1].scale.uses(anim_offset))
					{	
						bones[i].initScale.AnimRef.flags = 1;
						bones[i].initScale.AnimRef.animflag = 6;	
					}
				}
			}
#ifdef	ROOT_BONE
		}
#endif
		bones[i].ar1.AnimRef.animid = i | (6 << 16);
		bones[i].ar1.value = 1;
		bones[i].ar1.unValue = 1;
		f.Write(&bones[i], sizeof(BONE));
	}
	wxDELETEA(bones);
	f.Seek(datachunk_offset, wxFromStart);

	// nSkinnedBones
	mdata.nSkinnedBones = nSkinnedBones(m, &mpqf, 0, m->header.nVertices);
	//mdata.nSkinnedBones+=1;

	// vertFlags
	mdata.vertFlags = 0x182007D;

	// mBoneLU
	ModelView *view = (ModelView*)(mpqfv.getBuffer());
	ModelGeoset *ops = (ModelGeoset*)(mpqfv.getBuffer() + view->ofsSub);
	ModelTexUnit *tex = (ModelTexUnit*)(mpqfv.getBuffer() + view->ofsTex);
	uint16 *texlookup = (uint16*)(mpqf.getBuffer() + m->header.ofsTexLookup);
	uint16 *texunitlookup = (uint16*)(mpqf.getBuffer() + m->header.ofsTexUnitLookup);
	ModelVertex *verts = (ModelVertex*)(mpqf.getBuffer() + m->header.ofsVertices);
	uint16 *trianglelookup = (uint16*)(mpqfv.getBuffer() + view->ofsIndex);
	uint16 *triangles = (uint16*)(mpqfv.getBuffer() + view->ofsTris);
	uint16 *boneLookup = (uint16 *)(mpqf.getBuffer() + m->header.ofsBoneLookup);

	std::vector<uint16> bLookup;
	std::vector<uint16> bLookupcnt;
	for (size_t j=0; j<view->nSub; j++) {
		size_t geoset = j;//tex[j].op;
		std::vector<uint16> bLookup2; // local bLookup
		bLookup2.clear();

		for(uint32 i=ops[geoset].vstart; i<(ops[geoset].vstart+ops[geoset].vcount); i++) {
			for(uint32 k=0; k<4; k++) {
				if (verts[trianglelookup[i]].weights[k] != 0) {
					bool bFound = false;
					for(uint32 m=0; m<bLookup2.size(); m++) {
						if (bLookup2[m] == verts[trianglelookup[i]].bones[k])
							bFound = true;
					}
					if (bFound == false)
						bLookup2.push_back(verts[trianglelookup[i]].bones[k]);
				}
			}
		}
		bLookupcnt.push_back(bLookup2.size());
		// push local bLookup to global, and lookup it in boneLookup
		for(uint32 i=0; i<bLookup2.size(); i++) {
			//bLookup.push_back(boneLookup[bLookup2[i]]);
			bLookup.push_back(bLookup2[i]);
		}
	}

	// mVert
	mdata.mVert.nEntries = m->header.nVertices*sizeof(Vertex32);
	mdata.mVert.ref = ++fHead.nRefs;
	RefEntry("__8U", f.Tell(), mdata.mVert.nEntries, 0);
	for(uint32 i=0; i<m->header.nVertices; i++) {
		Vertex32 vert;
		memset(&vert, 0, sizeof(vert));
		vert.pos.x = verts[trianglelookup[i]].pos.y; 
		vert.pos.y = -verts[trianglelookup[i]].pos.x; 
		vert.pos.z = verts[trianglelookup[i]].pos.z;  
		//vert.pos = verts[i].pos;
		memcpy(vert.weBone, verts[trianglelookup[i]].weights, 4);

		uint16 tidx = 0;
		for (size_t j=0; j<view->nSub; j++) {
			size_t geoset = j;//tex[j].op;
			if (trianglelookup[i] >= ops[geoset].vstart && trianglelookup[i] < ops[geoset].vstart+ops[geoset].vcount)
				break;
			tidx += bLookupcnt[j];
		}

		int idx;
		for (uint32 k=0; k<4; k++)
		{
			if (verts[trianglelookup[i]].weights[k] != 0) {
				idx = -1;
				for(uint32 c=tidx; c<bLookup.size();c++)
				{
					if (verts[trianglelookup[i]].bones[k] == bLookup[c])
					{
						idx = c;
						break;
					}
				}
				if (idx == -1)
						vert.weIndice[k]  = 0;
				else
				{
					for(uint32 d=0;d<bLookupcnt.size();d++)
					{
						if (idx >= bLookupcnt[d])
							idx -= bLookupcnt[d];
						else
							break;
					}
					vert.weIndice[k] = idx;
				}
			}
		}
		//memcpy(vert.weIndice, verts[i].bones, 4);
		// Vec3D normal -> char normal[4], TODO
		vert.normal[0] = (verts[trianglelookup[i]].normal.x+1)*0xFF/2;
		vert.normal[1] = (verts[trianglelookup[i]].normal.y+1)*0xFF/2;
		vert.normal[2] = (verts[trianglelookup[i]].normal.z+1)*0xFF/2;
		// Vec2D texcoords -> uint16 uv[2], TODO
		vert.uv[0] = verts[trianglelookup[i]].texcoords.x*0x800;
		vert.uv[1] = verts[trianglelookup[i]].texcoords.y*0x800;
		f.Write(&vert, sizeof(vert));
	}
	padding(&f);

	// mDIV
	mdata.mDIV.nEntries = 1;
	mdata.mDIV.ref = ++fHead.nRefs;
	RefEntry("_VID", f.Tell(), mdata.mDIV.nEntries, 2);
	chunk_offset = f.Tell();
	DIV div;
	memset(&div, 0, sizeof(div));
	f.Seek(sizeof(div), wxFromCurrent);
	padding(&f);
	// mDIV.faces = m->view.nTriangles
	
	div.faces.nEntries = view->nTris;
	div.faces.ref = ++fHead.nRefs;
	RefEntry("_61U", f.Tell(), div.faces.nEntries, 0);
	
	for(uint16 i=0; i<div.faces.nEntries; i++) {
		uint16 tidx = 0;
		for (size_t j=0; j<view->nSub; j++) {
			size_t geoset = j;//tex[j].op;
			if (triangles[i] >= ops[geoset].vstart && triangles[i] < ops[geoset].vstart+ops[geoset].vcount)
			{
				tidx = triangles[i] - ops[geoset].vstart;
				break;
			}
		}
		//f.Write(&triangles[i], sizeof(uint16));
		f.Write(&tidx, sizeof(uint16));
	}
	padding(&f);
	// mDiv.meash
	div.REGN.nEntries = view->nSub;
	div.REGN.ref = ++fHead.nRefs;
	RefEntry("NGER", f.Tell(), div.REGN.nEntries, 3);

	int indBone = 0;
	for (size_t j=0; j<view->nSub; j++) {
		size_t geoset = j;//tex[j].op;

		REGN regn;
		memset(&regn, 0, sizeof(regn));
		regn.indFaces = ops[geoset].istart;
		regn.numFaces = ops[geoset].icount;
		regn.indVert = ops[geoset].vstart;
		regn.numVert = ops[geoset].vcount;
		regn.boneCount = bLookupcnt[j]; //nSkinnedBones(m, &mpqf, regn.indVert, regn.numVert);
		regn.indBone = indBone;
		regn.numBone = bLookupcnt[j]; //regn.boneCount;
		regn.b1[0] = regn.b1[1] = 1;
		indBone += regn.boneCount;
		f.Write(&regn, sizeof(regn));
	}
	padding(&f);


	//prepare BAT table

	ModelRenderFlags *renderflags = (ModelRenderFlags *)(mpqf.getBuffer() + m->header.ofsTexFlags);

	typedef struct {
		uint16 texid;
		uint16 flags;
		uint16 blend;
	} MATmap;

	typedef struct {
		uint16 subid;
		uint16 matid;
	} MeshMap;


	std::vector<MATmap> MATtable;
	std::vector<MeshMap> MeshtoMat;
	for (uint32 i=0; i<view->nTex; i++)
	{
		if (tex[i].texunit < m->header.nTexLookup && texunitlookup[tex[i].texunit] == 0)
		{	
			int idx = -1;

			for(uint32 j=0; j<MATtable.size(); j++)
			{
				if (MATtable[j].texid == texlookup[tex[i].textureid] && 
					MATtable[j].blend == renderflags[tex[i].flagsIndex].blend &&
					MATtable[j].flags == renderflags[tex[i].flagsIndex].flags)
				{
					idx = j;
					break;
				}
			}
			if (idx < 0)
			{
				MATmap bm;
				bm.texid = texlookup[tex[i].textureid];
				bm.flags = renderflags[tex[i].flagsIndex].flags;
				bm.blend = renderflags[tex[i].flagsIndex].blend;
				idx = MATtable.size();
				MATtable.push_back(bm);
				
				MeshMap mm;
				mm.subid = tex[i].op;
				mm.matid = idx;
				MeshtoMat.push_back(mm);
			}
			else
			{
				int found = 0;
				for(uint32 k=0; k<MeshtoMat.size(); k++)
				{
					if (MeshtoMat[k].subid == tex[i].op && MeshtoMat[k].matid == idx)
					{
						found = 1;
						break;
					}
				}
				if (found == 0)
				{
					MeshMap mm;
					mm.subid = tex[i].op;
					mm.matid = idx;
					MeshtoMat.push_back(mm);
				}
			}
		}
	}


	// mDiv.BAT
	div.BAT.nEntries = MeshtoMat.size(); //view->nSub;
	div.BAT.ref = ++fHead.nRefs;
	RefEntry("_TAB", f.Tell(), div.BAT.nEntries, 1);
	for (size_t j=0; j<div.BAT.nEntries; j++) {
		BAT bat;
		memset(&bat, 0, sizeof(bat));
		bat.subid = MeshtoMat[j].subid;
		bat.matid = MeshtoMat[j].matid;  // Error
		bat.s2 = -1;
		f.Write(&bat, 0xE); // sizeof(bat) is buggy to 0x10
	}
	padding(&f);
	// mDiv.MSEC
	div.MSEC.nEntries = 1;
	div.MSEC.ref = ++fHead.nRefs;
	RefEntry("CESM", f.Tell(), div.MSEC.nEntries, 1);
	MSEC msec;
	memset(&msec, 0, sizeof(msec));
	msec.bndSphere.AnimRef.animid = fHead.nRefs; // buggy
	f.Write(&msec, sizeof(msec));
	padding(&f);

	datachunk_offset = f.Tell();
	f.Seek(chunk_offset, wxFromStart);
	div.unk = 1;
	f.Write(&div, sizeof(div));
	f.Seek(datachunk_offset, wxFromStart);

	// mBoneLU
	mdata.mBoneLU.nEntries = bLookup.size();
	mdata.mBoneLU.ref = ++fHead.nRefs;
	RefEntry("_61U", f.Tell(), mdata.mBoneLU.nEntries, 0);
	for(uint16 i=0; i<bLookup.size(); i++) {
		uint16 idx = bLookup[i] + 1; 
		f.Write(&idx, sizeof(uint16));
	}
	padding(&f);

	// boundSphere
	mdata.boundSphere.min = m->header.ps.VertexBox[0];
	mdata.boundSphere.max = m->header.ps.VertexBox[1];
	mdata.boundSphere.radius = m->header.ps.VertexRadius;

	// mAttach
	// this makes some read errors in sc2 editor
#if 0
	if (m->header.nAttachments) {
		ModelAttachmentDef *attachments = (ModelAttachmentDef*)(mpqf.getBuffer() + m->header.ofsAttachments);
		mdata.mAttach.nEntries = m->header.nAttachments;
		mdata.mAttach.ref = ++fHead.nRefs;
		RefEntry("_TTA", f.Tell(), mdata.mAttach.nEntries, 0);
		chunk_offset = f.Tell();
		ATT *atts = new ATT[mdata.mAttach.nEntries];
		memset(atts, 0, sizeof(ATT)*mdata.mAttach.nEntries);
		f.Seek(sizeof(ATT)*mdata.mAttach.nEntries, wxFromCurrent);
		padding(&f);
		for(uint32 i=0; i<mdata.mAttach.nEntries; i++) {
			// name
			wxString strName = wxString::Format(_T("ATT_%d"), i);
			atts[i].name.nEntries = strName.Len()+1;
			atts[i].name.ref = ++fHead.nRefs;
			RefEntry("RAHC", f.Tell(), atts[i].name.nEntries, 0);
			f.Write(strName.c_str(), strName.Len()+1);
			padding(&f);
		}
		datachunk_offset = f.Tell();
		f.Seek(chunk_offset, wxFromStart);
		for(uint32 i=0; i<mdata.mAttach.nEntries; i++) {
			atts[i].flag = -1;
			atts[i].bone = attachments[i].bone;
			f.Write(&atts[i], sizeof(ATT));
		}
		wxDELETEA(atts);
		f.Seek(datachunk_offset, wxFromStart);
	}

	// mAttachLU
	//int16 *attachLookup = (int16 *)(mpqf.getBuffer() + m->header.ofsAttachLookup);
	if (mdata.mAttach.nEntries) {
		mdata.mAttachLU.nEntries = mdata.mAttach.nEntries;
		mdata.mAttachLU.ref = ++fHead.nRefs;
		RefEntry("_61U", f.Tell(), mdata.mAttachLU.nEntries, 0);
		for(uint16 i=0; i<mdata.mAttachLU.nEntries; i++) {
			int16 ii = -1;
			f.Write(&ii, sizeof(int16));
		}
		padding(&f);
	}
#endif
	// mMatLU
	if (MATtable.size() > 0) {
		mdata.mMatLU.nEntries = MATtable.size();
		mdata.mMatLU.ref = ++fHead.nRefs;
		RefEntry("MTAM", f.Tell(), mdata.mMatLU.nEntries, 0);
		for(uint32 i=0; i<mdata.mMatLU.nEntries; i++) {
			MATM matm;
			matm.nmat = 1;
			matm.matind = i;
			f.Write(&matm, sizeof(matm));
		}
		padding(&f);
	}

	// mMat
	if (MATtable.size() > 0) {
		mdata.mMat.nEntries = MATtable.size();
		mdata.mMat.ref = ++fHead.nRefs;
		RefEntry("_TAM", f.Tell(), mdata.mMat.nEntries, 0xF);
		chunk_offset = f.Tell();
		MAT *mats = new MAT[mdata.mMat.nEntries];
		memset(mats, 0, sizeof(MAT)*mdata.mMat.nEntries);
		f.Seek(sizeof(MAT)*mdata.mMat.nEntries, wxFromCurrent);
		padding(&f);
		for(uint32 i=0; i<mdata.mMat.nEntries; i++) {
			// name
			wxString ff = wxString(fn, wxConvUTF8).BeforeLast('.').AfterLast(SLASH);
			wxString strName = wxString::Format(_T("%sMat_%02d"), ff.c_str(), i+1);
			mats[i].name.nEntries = strName.Len()+1;
			mats[i].name.ref = ++fHead.nRefs;
			RefEntry("RAHC", f.Tell(), mats[i].name.nEntries, 0);
			f.Write(strName.c_str(), strName.Len()+1);
			padding(&f);

			// layers
			int chunk_offset2, datachunk_offset2;
			for(uint32 j=0; j<13; j++) {
				mats[i].layers[j].nEntries = 1;
				mats[i].layers[j].ref = ++fHead.nRefs;
				RefEntry("RYAL", f.Tell(), mats[i].layers[j].nEntries, 0x16);
				chunk_offset2 = f.Tell();
				LAYR layer;
				memset(&layer, 0, sizeof(layer));
				layer.flags = 236;
				layer.brightness_mult1.value = 1.0f;
				layer.brightness_mult1.unValue = 1.0f;
				layer.uvTiling.value = Vec2D(1.0f, 1.0f);
				layer.d20 = -1;
				layer.colour[0] = 255;
				layer.colour[1] = 255;
				layer.colour[2] = 255;
				layer.colour[3] = 255;
				f.Write(&layer, sizeof(layer));
				padding(&f);

				if (j == 0) { // LAYER_Diff
					int texid = MATtable[i].texid;
					wxString strName = wxString(m->TextureList[texid].c_str(), wxConvUTF8).BeforeLast('.').AfterLast(SLASH);
					strName.Append(_T(".tga"));
					layer.name.nEntries = strName.Len()+1;
					layer.name.ref = ++fHead.nRefs;
					RefEntry("RAHC", f.Tell(), layer.name.nEntries, 0);
					f.Write(strName.c_str(), strName.Len()+1);
					padding(&f);
				}

				if (j == 7 && MATtable[i].blend == 1) // LAYER_Alpha
				{
					int texid = MATtable[i].texid;
					wxString strName = wxString(m->TextureList[texid].c_str(), wxConvUTF8).BeforeLast('.').AfterLast(SLASH);
					strName.Append(_T(".tga"));
					layer.name.nEntries = strName.Len()+1;
					layer.name.ref = ++fHead.nRefs;
					RefEntry("RAHC", f.Tell(), layer.name.nEntries, 0);
					layer.renderFlags = 2;   // Alpha Only
					f.Write(strName.c_str(), strName.Len()+1);
					padding(&f);
				}

				datachunk_offset2 = f.Tell();
				f.Seek(chunk_offset2, wxFromStart);
				f.Write(&layer, sizeof(layer));
				f.Seek(datachunk_offset2, wxFromStart);
			}
		}
		datachunk_offset = f.Tell();
		f.Seek(chunk_offset, wxFromStart);
		for(uint32 i=0; i<mdata.mMat.nEntries; i++) {
			mats[i].cutoutThresh = 0xC8;
			mats[i].SpecMult = 1;
			mats[i].EmisMult = 1;
			mats[i].layerBlend = 2;
			mats[i].emisBlend = 2;
			mats[i].d5 = 2;
			if (MATtable[i].blend == 3)
				mats[i].blendMode = 2; 
			else
				mats[i].blendMode = 0; 
			if (MATtable[i].flags & RENDERFLAGS_UNLIT)
				mats[i].flags |= MAT_FLAG_UNSHADED;
			if (MATtable[i].flags & RENDERFLAGS_UNFOGGED)
				mats[i].flags |= MAT_FLAG_UNFOGGED;
			if (MATtable[i].flags & RENDERFLAGS_TWOSIDED)
				mats[i].flags |= MAT_FLAG_TWOSIDED;
			if (MATtable[i].flags & RENDERFLAGS_BILLBOARD)
				mats[i].flags |= MAT_FLAG_DEPTHPREPASS;
			f.Write(&mats[i], sizeof(MAT));
		}
		wxDELETE(mats);
		f.Seek(datachunk_offset, wxFromStart);
	}

	// mIREF
	mdata.mIREF.nEntries = mdata.mBone.nEntries;
	mdata.mIREF.ref = ++fHead.nRefs;
	RefEntry("FERI", f.Tell(), mdata.mIREF.nEntries, 0);
	for(uint32 i=0; i<mdata.mIREF.nEntries; i++) {
		IREF iref;
		memset(&iref, 0, sizeof(iref));
		iref.matrix[0][1] = 1.0f;
		iref.matrix[1][0] = -1.0f;
		iref.matrix[2][2] = 1.0f;
#ifdef	ROOT_BONE
		if (i > 0 && i < mdata.mIREF.nEntries - 1) {
			iref.matrix[3][0] = -m->bones[i-1].pivot.x;
			iref.matrix[3][1] = m->bones[i-1].pivot.z;
			iref.matrix[3][2] = -m->bones[i-1].pivot.y;
		} 
#else
		iref.matrix[3][0] = -m->bones[i].pivot.x;
		iref.matrix[3][1] = m->bones[i].pivot.z;
		iref.matrix[3][2] = -m->bones[i].pivot.y;
#endif
		iref.matrix[3][3] = 1.0f;
		f.Write(&iref, sizeof(iref));
	}
	padding(&f);

	//
	//mdata.mat.a = Vec4D(1.0f, 0.0f, 0.0f, 0.0f);
	//mdata.mat.b = Vec4D(0.0f, 1.0f, 0.0f, 0.0f);
	//mdata.mat.c = Vec4D(0.0f, 0.0f, 1.0f, 0.0f);
	//mdata.mat.d = Vec4D(0.0f, 0.0f, 0.0f, 1.0f);

	// 4. ReferenceEntry
	fHead.ofsRefs = f.Tell();
	fHead.nRefs = reList.size(); // MODL Ref

	for(size_t i=0; i<reList.size(); i++) {
		f.Write(&reList[i], sizeof(ReferenceEntry));
	}

	// 5. rewrite head
	f.Seek(0, wxFromStart);
	f.Write(&fHead, sizeof(fHead));
	f.Write(&mdata, sizeof(mdata));

	// save textures
	for (size_t i=0; i<m->passes.size(); i++) {
		ModelRenderPass &p = m->passes[i];

		if (p.init(m)) {
			wxString texName = wxString(m->TextureList[p.tex].c_str(), wxConvUTF8).BeforeLast(_T('.')).AfterLast(SLASH);
			texName.Append(_T(".tga"));

			wxString texFilename(fn, wxConvUTF8);
			texFilename = texFilename.BeforeLast(SLASH);
			texFilename += SLASH;
			texFilename += texName;
			wxLogMessage(_T("Exporting Image: %s"),texFilename.c_str());
			SaveTexture(texFilename);
		}
	}

	wxDELETEA(seqs);
	wxDELETEA(stcs);

	mpqf.close();
	mpqfv.close();
	f.Close();
}