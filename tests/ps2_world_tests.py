"""Source-backed offline regressions; no GTA process or renderer is executed."""
import csv, json, math, pathlib, struct, subprocess, sys, tempfile, unittest
sys.path.insert(0,str(pathlib.Path(__file__).resolve().parents[1]/'tools'))
from ps2_world import Game, Level, Chunk
from ps2_export import collision, mesh_file, bound_collision, SURFACES, PHYSICAL_DOOR_MODEL_NAMES
from ps2_names import TextureNames,TextureDictionaries,image_key
from ps2_visibility import assemblies,select_layers,visible,time_compatible
from ps2_layout import quaternion,lod_graph,partitions,sa_flags,level_at,ISLAND_LEVELS
from ps2_water import export_water
from ps2_input import extract_iso,REQUIRED

ROOT=pathlib.Path(__file__).resolve().parents[1]
TEST_TEMP=ROOT/'build'/'ps2-test-temp'
TEST_TEMP.mkdir(parents=True,exist_ok=True)


class SourceWorld(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.game=Game(ROOT / 'local/originals/lcs-ps2',ROOT/'vendor/librwgta/librwgta-master/tools/storiesconv/hashes.inc')
        cls.level=Level(ROOT / 'local/originals/lcs-ps2','INDUST')

    def test_railway_near_and_artist_lod_resources_are_separate(self):
        links=json.loads((ROOT/'reports/ps2-verified-links-v13.json').read_text())
        groups=assemblies(self.game,self.level,links['links'])
        for group in groups[3351]:group['level']=self.level
        layers=list(select_layers(self.game.models[3351],groups[3351]))
        self.assertEqual(len(layers),2)
        near,far=layers
        self.assertTrue(near['primary']);self.assertFalse(far['primary'])
        self.assertEqual({r['resource'] for r in near['rows']},{1783,1784})
        self.assertEqual({r['resource'] for r in far['rows']},{1785})
        self.assertEqual(sum(len(m['triangles']) for r in near['rows'] for m in self.level.building(r['resource'])),202)
        self.assertEqual(sum(len(m['triangles']) for r in far['rows'] for m in self.level.building(r['resource'])),14)
        self.assertEqual(far['conditions']['atomicLod'],1)

    def test_original_track_collision_covers_track_deck(self):
        c,p=self.game.collisions[3351];self.assertEqual(c.read('<3h',p+48),(0,0,34))
        pv,pt=c.read('<2I',p+76);hits=[]
        # Vertical ray at the local centre must meet an upward-facing deck.
        for a,b,d,mat,_ in c.read_array('<3H2B',pt,34):
            verts=[tuple(v/128 for v in c.read('<3h',pv+i)) for i in (a,b,d)]
            (x1,y1,z1),(x2,y2,z2),(x3,y3,z3)=verts
            det=(y2-y3)*(x1-x3)+(x3-x2)*(y1-y3)
            if abs(det)<1e-9:continue
            u=((y2-y3)*(-x3)+(x3-x2)*(-y3))/det
            v=((y3-y1)*(-x3)+(x1-x3)*(-y3))/det
            if min(u,v,1-u-v)>=-1e-6:hits.append(u*z1+v*z2+(1-u-v)*z3)
        self.assertTrue(hits,'Original railway has no deck at its centre')
        self.assertGreater(max(hits),-.1)

    def test_station_has_both_original_render_parts(self):
        rows=[r for r in self.level.instances[786] if not r['interior']]
        self.assertEqual({r['resource'] for r in rows},{1047,1070})
        self.assertEqual({r['renderpass'] for r in rows},{2,6})
        entity=next(e for e in self.game.entities if e['model']==905)
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            out=pathlib.Path(directory)/'petrolstation.psm'
            result=mesh_file(self.level,rows,entity,out)
            self.assertEqual(result['triangles'],1929)
            self.assertGreaterEqual(result['vertices'],3616)
            raw=out.read_bytes();_,nv,nt,nm=struct.unpack_from('<4s3I',raw)
            start=16+32*nm
            actual={tuple(raw[start+24*i+20:start+24*i+24]) for i in range(nv)}
            original={rgba for row in rows for mesh in self.level.building(row['resource']) for _,_,rgba in mesh['vertices']}
            self.assertEqual(actual,original)
            self.assertGreater(len(actual),10)
            self.assertTrue(any(min(r[:3])<128 for r in actual))
            self.assertEqual(len(raw),start+24*nv+8*nt)

    def test_original_island_views_are_selected_per_level(self):
        self.assertEqual(len(self.game.map_zones),104)
        for point,level,expected in (((880,-542,20),1,{'islandlodcomind','islandlodsubind'}),
                                      ((0,-600,30),2,{'islandlodind','islandlodsubcom'}),
                                      ((-1100,-700,30),3,{'islandlodind','islandlodcomsub'})):
            self.assertEqual(level_at(self.game,point),level)
            self.assertEqual({n for n,levels in ISLAND_LEVELS.items() if level in levels},expected)

    def test_lod_relationship_and_sector_world_transform(self):
        self.assertEqual(self.game.models[691]['related'],905)
        self.assertEqual(self.game.models[905]['name'],'petrolstation')
        self.assertEqual(self.game.models[905]['txd'],'8ballsB')
        near=self.level.instances[786][0]
        lod=self.level.instances[4965][0]
        self.assertGreater(near['copies'],1);self.assertGreater(lod['copies'],1)
        self.assertEqual(lod['renderpass'],1)
        self.assertAlmostEqual(near['matrix'][12],1160.3114,places=3)
        self.assertAlmostEqual(near['matrix'][13],-86.31305,places=3)

    def test_water_preserves_all_cells_and_native_capacities(self):
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            out=pathlib.Path(directory)/'water.dat';result=export_water(self.game,out)
            self.assertTrue(result['allSourceCellsVerified'])
            self.assertEqual(result['occupiedCells'],12892)
            self.assertEqual(result['quads'],177)
            self.assertLessEqual(result['vertices'],1021)
            self.assertLessEqual(result['polyCombos'],700)
            heights=set()
            for line in out.read_text().splitlines()[1:]:
                values=line.split();self.assertEqual(len(values),29)
                heights.add(float(values[2]))
            self.assertEqual(heights,{0.,63.200001})

    def test_collision_indices_surfaces_and_enclosing_bounds(self):
        pavement_faces=0
        for model,row in self.game.models.items():
            if row['type'] not in (1,3) or not row['name'] or model not in self.game.collisions:continue
            raw=collision(self.game,model,version=1)
            self.assertEqual(raw[:4],b'COLL');self.assertEqual(len(raw),struct.unpack_from('<I',raw,4)[0]+8)
            self.assertEqual(raw[8:30].split(b'\0')[0].decode(),row['name'])
            radius,x,y,z,*bounds=struct.unpack_from('<10f',raw,32)
            p=72;ns=struct.unpack_from('<I',raw,p)[0];p+=4
            import math
            for i in range(ns):
                r,a,b,c,mat,*_=struct.unpack_from('<4f4B',raw,p+20*i)
                self.assertLessEqual(math.dist((x,y,z),(a,b,c))+r,radius+.001)
                self.assertLessEqual(mat,70)
                self.assertEqual(raw[p+20*i+19],255)
            p+=20*ns;nl=struct.unpack_from('<I',raw,p)[0];p+=4+24*nl
            nb=struct.unpack_from('<I',raw,p)[0];p+=4
            for i in range(nb):self.assertEqual(raw[p+28*i+27],255)
            p+=28*nb
            nv=struct.unpack_from('<I',raw,p)[0];p+=4
            for v in struct.iter_unpack('<3f',raw[p:p+12*nv]):
                self.assertLessEqual(math.dist((x,y,z),v),radius+.001)
                for j in range(3):self.assertLessEqual(bounds[j]-.001,v[j]);self.assertLessEqual(v[j],bounds[j+3]+.001)
            p+=12*nv;nt=struct.unpack_from('<I',raw,p)[0];p+=4
            source,offset=self.game.collisions[model]
            original=list(source.read_array('<3H2B',source.u32(offset+80),nt))
            for i,face in enumerate(original):
                material=raw[p+16*i+12]
                self.assertEqual(raw[p+16*i+13:p+16*i+16],b'\x00\x00\xff')
                self.assertEqual(material,SURFACES[face[3]] if face[3]<len(SURFACES) else 0)
                if face[3]==5:
                    self.assertEqual(material,4) # LCS PAVEMENT -> SA PAVEMENT
                    self.assertNotEqual(material,26) # SA DIRT
                    pavement_faces+=1
            for a,b,c,mat,*_ in struct.iter_unpack('<3I4B',raw[p:p+16*nt]):
                self.assertLess(max(a,b,c),nv);self.assertLessEqual(mat,70)
            self.assertEqual(p+16*nt,len(raw))
        self.assertEqual(pavement_faces,16936)

    def test_garage_door_collision_bounds_preserve_native_travel(self):
        checked=0
        for model,row in self.game.models.items():
            if (row['name'] or '').lower() not in PHYSICAL_DOOR_MODEL_NAMES or model not in self.game.collisions:continue
            chunk,at=self.game.collisions[model]
            expected=(*chunk.read('<3f',at+16),*chunk.read('<3f',at+32))
            raw=collision(self.game,model)
            self.assertEqual(struct.unpack_from('<6f',raw,32),expected,row['name'])
            if row['name']=='ind_slidedoor':
                height=struct.unpack_from('<f',raw,52)[0]-struct.unpack_from('<f',raw,40)[0]-.1
                self.assertAlmostEqual(height,5.48223819732666,places=5)
            checked+=1
        self.assertGreater(checked,20)

    def test_native_col2_preserves_source_coordinates_surfaces_and_white_lighting(self):
        faces=0;models=0;pavement=0
        for model,row in self.game.models.items():
            if row['type'] not in (1,3) or not row['name'] or model not in self.game.collisions:continue
            raw=collision(self.game,model)
            self.assertEqual(raw[:4],b'COL2');self.assertEqual(len(raw),8+struct.unpack_from('<I',raw,4)[0])
            ns,nb,nt,nl,pad,flags,*offsets=struct.unpack_from('<3H2BI6I',raw,72)
            self.assertEqual((nl,pad,flags,offsets[2],offsets[5]),(0,0,2 if ns or nb or nt else 0,0,0))
            chunk,at=self.game.collisions[model];self.assertEqual((ns,nb,nt),chunk.read('<3h',at+48))
            points=4+offsets[3];triangles=4+offsets[4]
            for i,(a,b,c,material,_source_padding) in enumerate(chunk.read_array('<3H2B',chunk.u32(at+80),nt)):
                actual=struct.unpack_from('<3H2B',raw,triangles+8*i)
                self.assertEqual(actual,(a//6,b//6,c//6,SURFACES[material] if material<35 else 0,255))
                for vertex in (a,b,c):
                    self.assertEqual(raw[points+vertex:points+vertex+6],chunk.span(chunk.u32(at+76)+vertex,6))
                if material==5:self.assertEqual(actual[3],4);pavement+=1
            for off,count,stride in ((offsets[0],ns,20),(offsets[1],nb,28)):
                for i in range(count):self.assertEqual(raw[4+off+stride*i+stride-1],255)
            self.assertEqual(len(raw)%4,0)
            faces+=nt;models+=1
        self.assertEqual((models,faces,pavement),(3400,159293,16936))

    def test_named_rsl_textures_and_exact_correspondence(self):
        names=TextureNames(self.game)
        self.assertEqual(len(names.records),3116)
        self.assertIn('generic',names.by_dictionary)
        self.assertIn('wheel_alloy_q',names.by_dictionary['generic'])
        image=names.by_dictionary['bogroll']['white']
        self.assertEqual(names.identify(image,'bogroll')['name'],'white')
        altered=(*image[:2],bytes([image[2][0]^1])+image[2][1:])
        self.assertIsNone(names.identify(altered,'bogroll')['name'])
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            rows=[r for r in self.level.instances[786] if not r['interior']]
            entity=next(e for e in self.game.entities if e['model']==905)
            path=pathlib.Path(directory)/'station.psm'
            ids={mesh['texture'] for row in rows for mesh in self.level.building(row['resource'])}
            resolved={id:names.identify(self.level.texture(id),'8ballsB')['name'] for id in ids}
            confirmed={id:name for id,name in resolved.items() if name}
            self.assertGreater(len(confirmed),0)
            mesh_file(self.level,rows,entity,path,confirmed)
            raw=path.read_bytes();nm=struct.unpack_from('<I',raw,12)[0]
            materials={raw[16+32*i:48+32*i].split(b'\0')[0].decode() for i in range(nm)}
            self.assertTrue(set(confirmed.values())<=materials)

    def test_same_original_name_different_images_preserves_both(self):
        names=TextureNames(self.game)
        routes=TextureDictionaries(names.by_dictionary)
        for resource,dictionary,model,texture in ((18,'rp_bridge','bridge_support','concretemanky'),
                                                (128,'dyn_market','fishstall04','cratetop128')):
            image=self.level.texture(resource)
            record=names.identify(image,dictionary,model)
            self.assertEqual(record['name'],texture)
            self.assertTrue(record['dictionaryConflict'])
            original=names.by_dictionary[dictionary][texture]
            self.assertNotEqual(image_key(image),image_key(original))
            target=routes.assign(dictionary,{texture:image})
            self.assertNotEqual(target,dictionary)
            self.assertEqual(routes.textures[target][texture],image)
            routes.add_resident(dictionary,names.by_dictionary[dictionary])
            self.assertEqual(routes.textures[dictionary][texture],original)
            self.assertEqual(routes.assign(dictionary,{texture:image}),target)
            self.assertEqual(routes.assign(dictionary,{texture:original}),dictionary)

    def test_empty_source_dictionary_is_part_of_the_model_catalog(self):
        names=TextureNames(self.game);routes=TextureDictionaries(names.by_dictionary)
        self.assertEqual(self.game.models[2544]['txd'],'subglass')
        self.assertEqual(self.game.models[2545]['txd'],'subglass')
        self.assertIn('subglass',self.game.txds.values())
        self.assertEqual(names.by_dictionary.get('subglass',{}),{})
        routes.add_resident('subglass',names.by_dictionary.get('subglass',{}))
        self.assertIn('subglass',routes.textures)
        self.assertEqual(routes.textures['subglass'],{})

    def test_dictionary_conflicts_use_sa_case_insensitive_lookup(self):
        first=(1,1,b'\x10\x20\x30\xff');second=(1,1,b'\x40\x50\x60\xff')
        routes=TextureDictionaries({'source':{'Pavement':first}})
        target=routes.assign('source',{'pavement':second})
        self.assertNotEqual(target,'source')
        self.assertEqual(routes.textures[target],{'pavement':second})
        routes.add_resident('source',{'Pavement':first})
        self.assertEqual(routes.textures['source']['Pavement'],first)

    def test_resource_overlays_preserve_time_and_story_predicates(self):
        level=Level(ROOT / 'local/originals/lcs-ps2','COMMER')
        night=level.conditions(level.instances[2894][0]);day=level.conditions(level.instances[3531][0])
        self.assertIsNone(night['instance'])
        self.assertEqual(night['resources'],[[134,19]])
        self.assertEqual(day['resources'],[[147,6]])
        for hour in (0,5,6,12,18,19,23):
            self.assertEqual(visible(night,hour,{}),hour<6 or hour>=19)
            self.assertEqual(visible(day,hour,{}),6<=hour<19)
        self.assertTrue(time_compatible(self.game.models[865],level,level.instances[2894]))
        self.assertFalse(time_compatible(self.game.models[1171],level,level.instances[2894]))
        story=level.conditions(level.instances[6483][0])
        self.assertFalse(visible(story,12,{}));self.assertTrue(visible(story,12,{11:1}))
        self.assertFalse(visible(story,12,{11:2}))

    def test_luigi_club_states_cannot_merge_into_one_dff(self):
        level=Level(ROOT / 'local/originals/lcs-ps2','COMMER')
        links=json.loads((ROOT/'reports/ps2-verified-links-v6.json').read_text())['links']
        groups=assemblies(self.game,level,links)[3754]
        for group in groups:group['level']=level
        layers=list(select_layers(self.game.models[3754],groups))
        first=next(l for l in layers if any(r['resource']==4597 for r in l['rows']))
        second=next(l for l in layers if any(r['resource']==4600 for r in l['rows']))
        self.assertNotEqual(first['name'],second['name'])
        self.assertNotEqual(first['conditions'],second['conditions'])
        self.assertFalse(any(r['resource']==4600 for r in first['rows']))
        self.assertFalse(any(r['resource']==4597 for r in second['rows']))
        self.assertEqual(sum(l['primary'] for l in layers),1)
        self.assertTrue(visible(first['conditions'],12,{11:1}))
        self.assertFalse(visible(second['conditions'],12,{11:1}))

    def test_interior_sectors_replace_exterior_and_shared_copies_render_once(self):
        level=Level.__new__(Level)
        level.exterior_count=1
        level.coordinates={i:(3,4) for i in range(3)}
        level.interior_choices={(3,4):[(7,1,1),(13,1,2)]}
        for mask in range(1,8):
            copies={i for i in range(3) if mask&(1<<i)}
            rule=dict(instance=None,resources=[],interiors=[],
                      sectorAlternatives=level.sector_conditions(dict(sector=min(copies),sectors=copies)))
            for first in (0,1):
                for second in (0,1):
                    selected=1 if first else 2 if second else 0
                    self.assertEqual(visible(rule,12,{7:first,13:second}),selected in copies)
        self.assertEqual(level.sector_conditions(dict(sector=0,sectors={0,1,2})),[])
        source=Level(ROOT / 'local/originals/lcs-ps2','COMMER')
        self.assertTrue(any(any(s<source.exterior_count for s in r['sectors'])
                            and any(s>=source.exterior_count for s in r['sectors'])
                            for rows in source.instances.values() for r in rows))
        for rows in source.instances.values():
            signatures=[(r['resource'],r['renderpass']) for r in rows]
            self.assertEqual(len(signatures),len(set(signatures)))

    def test_sa_quaternions_reconstruct_every_original_entity_basis(self):
        for entity in self.game.entities:
            basis=[entity['matrix'][i:i+3] for i in (0,4,8)]
            x,y,z,w=quaternion(basis);x,y,z=-x,-y,-z
            reconstructed=((1-2*(y*y+z*z),2*(x*y+w*z),2*(x*z-w*y)),
                           (2*(x*y-w*z),1-2*(x*x+z*z),2*(y*z+w*x)),
                           (2*(x*z+w*y),2*(y*z-w*x),1-2*(x*x+y*y)))
            self.assertLess(max(abs(a-b) for old,new in zip(basis,reconstructed) for a,b in zip(old,new)),.0001)

    def test_lod_identity_and_partition_indices_are_preserved(self):
        graph=lod_graph(self.game)
        gas=next(p for p in graph['pairs'] if p['lodModel']==691)
        self.assertEqual(gas['nearModel'],905)
        self.assertEqual(graph['sourceLodInstances'],len(graph['pairs'])+len(graph['pending']))
        rows=[dict(lod=-1) for _ in range(9)];rows[0]['lod']=8;rows[1]['lod']=8
        groups=partitions(rows,4)
        self.assertEqual(sorted(i for group in groups for i in group),list(range(9)))
        self.assertTrue(any(all(i in group for i in (0,1,8)) for group in groups))
        self.assertTrue(all(len(group)<=4 for group in groups))
        self.assertEqual(sa_flags((1<<2)|(1<<6)|(1<<7)|(1<<10)|(1<<11)|(1<<13)|(1<<14)),1|4|8|64|128|512|1024)

    def test_render_layer_col2_has_bounds_without_physical_surfaces(self):
        raw=bound_collision('source_layer',[-5,-2,-1,7,9,3])
        self.assertEqual(raw[:4],b'COL2');self.assertEqual(len(raw),108)
        self.assertEqual(struct.unpack_from('<3H2BI6I',raw,72),(0,)*12)
        mnx,mny,mnz,mxx,mxy,mxz,x,y,z,radius=struct.unpack_from('<10f',raw,32)
        self.assertEqual((x,y,z),(1,3.5,1));self.assertGreater(radius,math.dist((x,y,z),(7,9,3)))

    def test_story_prototype_uses_its_original_near_frame_and_preserves_ambiguity(self):
        metadata=json.loads((ROOT/'reports/ps2-verified-links-v10.json').read_text())
        link=next(r for r in metadata['links'] if r['level']=='COMMER' and r['worldId']==6497)
        self.assertEqual((link['model'],link['sourceEntityModel'],link['entityIndex']),(3703,3607,1496))
        self.assertEqual(link['variantFromRelatedLodModel'],3724)
        self.assertEqual(self.game.models[3724]['related'],3703)
        self.assertTrue(link['referenceVerticesAllMatched'])
        self.assertTrue(all(r['frameOnly'] and len(r['frameAliases'])==3 for r in metadata['prototypeLinks']))
        self.assertEqual(len(metadata['prototypeLinks']),6)
        level=Level(ROOT / 'local/originals/lcs-ps2','COMMER')
        entity=next(e for e in self.game.entities if e['pool']=='building' and e['index']==1496)
        wrong=next(e for e in self.game.entities if e['model']==3724)
        self.assertNotEqual(entity['matrix'][12:15],wrong['matrix'][12:15])
        with (ROOT/'reports/reference-model-bounds.csv').open(encoding='utf-8-sig') as source:
            reference=next(r for r in csv.DictReader(source) if r['name'].lower()=='fs_tenc_d')
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            result=mesh_file(level,level.instances[6497],entity,pathlib.Path(directory)/'variant.psm')
        expected=[float(reference[k]) for k in ('minx','miny','minz','maxx','maxy','maxz')]
        self.assertLess(max(abs(a-b) for a,b in zip(result['bounds'],expected)),.03)

    def test_unverified_proposals_cannot_create_a_conversion_or_public_bundle(self):
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            directory=pathlib.Path(directory);proposal=directory/'proposal.json'
            proposal.write_text(json.dumps(dict(geometryVerified=False,links=[])))
            for script,extra in (('convert-ps2-map.py',[str(ROOT/'not-a-rom')]),
                                 ('package-converter.py',[])):
                output=directory/script
                command=[sys.executable,str(ROOT/'tools'/script),*extra,str(output),'--links',str(proposal)]
                if script=='package-converter.py':command+=['--python',str(ROOT),'--crt',str(ROOT)]
                result=subprocess.run(command,capture_output=True,text=True)
                self.assertNotEqual(result.returncode,0)
                self.assertIn('Proposed source associations',result.stderr)
                self.assertFalse(output.exists())

    def test_bounds_readers_reject_corrupt_inputs(self):
        with self.assertRaises(ValueError):Chunk(b'\0'*32)
        with self.assertRaises(ValueError):self.level.chunk.span(len(self.level.chunk.data),1)
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            bad=pathlib.Path(directory)/'bad.iso';bad.write_bytes(b'\0'*40*2048)
            with self.assertRaises(ValueError):extract_iso(bad,pathlib.Path(directory)/'out')
            self.assertFalse((pathlib.Path(directory)/'out').exists())

    def test_iso_reads_required_files_and_rejects_outside_extent(self):
        # Generated ISO directory records only, without any copyrighted input.
        raw=bytearray(40*2048)
        def entry(name,sector,size,directory=False):
            name=name.encode('ascii');record=bytearray((34+len(name))&~1)
            record[0]=len(record)
            struct.pack_into('<I',record,2,sector);struct.pack_into('>I',record,6,sector)
            struct.pack_into('<I',record,10,size);struct.pack_into('>I',record,14,size)
            record[25]=2 if directory else 0
            record[28:32]=b'\x01\0\0\x01';record[32]=len(name);record[33:33+len(name)]=name
            return record
        def directory(sector,records):
            data=b''.join(records);self.assertLessEqual(len(data),2048)
            raw[sector*2048:sector*2048+len(data)]=data
        pvd=bytearray(2048);pvd[:7]=b'\x01CD001\x01'
        struct.pack_into('<H',pvd,128,2048);struct.pack_into('>H',pvd,130,2048)
        record=entry('\0',20,2048,True);pvd[156:156+len(record)]=record
        raw[16*2048:17*2048]=pvd
        directory(20,[entry('CHK',21,2048,True),entry('MODELS',23,2048,True)])
        directory(21,[entry('PS2',22,2048,True)])
        payloads={name:('synthetic '+name).encode('ascii') for name in REQUIRED}
        directory(22,[entry('GAME.DTZ;1',24,len(payloads['CHK/PS2/GAME.DTZ']))])
        directory(23,[entry(pathlib.PurePosixPath(name).name+';1',25+i,len(payloads[name]))
                      for i,name in enumerate(REQUIRED[1:])])
        for i,name in enumerate(REQUIRED):
            data=payloads[name];raw[(24+i)*2048:(24+i)*2048+len(data)]=data
        with tempfile.TemporaryDirectory(dir=TEST_TEMP) as directory:
            directory=pathlib.Path(directory);iso=directory/'fixture.iso';iso.write_bytes(raw)
            output=extract_iso(iso,directory/'extracted')
            for name,data in payloads.items():self.assertEqual((output/name).read_bytes(),data)
            # First MODELS record points past EOF; reject before creating output.
            struct.pack_into('<I',raw,23*2048+2,1000)
            struct.pack_into('>I',raw,23*2048+6,1000)
            iso.write_bytes(raw)
            with self.assertRaises(ValueError):extract_iso(iso,directory/'bad-extent')
            self.assertFalse((directory/'bad-extent').exists())


if __name__=='__main__':unittest.main()
