"""Original retail route storage/consumer prerequisites, no runtime support."""
import importlib.util
import pathlib
import sys
ROOT=pathlib.Path(__file__).resolve().parents[1]
spec=importlib.util.spec_from_file_location('route_original_inspect',ROOT/'tools/inspect-original-command.py')
module=importlib.util.module_from_spec(spec)
sys.modules[spec.name]=module
spec.loader.exec_module(module)
image=module.OriginalImage(ROOT / 'local/originals/lcs-ps2/SLUS_214.23')
image.require_profile()
expected={
    0x1D200C:0xAFC00250,  # real CPed constructor: current objective NONE
    0x1D2014:0xAFC00254,  # previous objective NONE, not inferred from tasks
    0x20BD74:0x24060003,  # FOLLOW_ROUTE collects ped, route id, mode
    0x20BDB0:0x0C0C22D0,  # real SetObjective(30, halfword, halfword)
    0x20BDD0:0x24060004,  # ADD_ROUTE_POINT consumes route id, XYZ
    0x20BDEC:0x8624F320,  # signed halfword identity
    0x20BDF8:0x0C0BD62C,  # AddRoutePoint 2F58B0
    0x2292FC:0x24060001,  # REMOVE_ROUTE takes one id
    0x229308:0x0C0BD5D6,  # RemoveRoute 2F5758
    0x2F58E4:0x2C8300C8,  # original capacity 200
    0x2F58E8:0x10600005,  # FULL branches to unconditional writer with index200
    0x2F5900:0x2522EB10,  # not a legitimate full-pool no-op; contain OOB write
    0x2F58C4:0x2402FFFF,  # free sentinel -1
    0x2F5904:0x00041940,  # stride 32 bytes
    0x2F590C:0xA4880000,  # halfword route assignment
    0x2F5910:0x24420010,  # position at node+16
    0x2F5978:0x2CA300C8,  # first route point scans 200 entries
    0x2F5988:0x2402FFFF,  # absent first point -1
    0x2F57F8:0x0120502D,  # copy end bound is old last_point, not capacity
    0x2F583C:0x010A102B,  # while advancing destination < original last_point
    0x2F586C:0x004C1023,  # tail begins capacity - removed count
    0x2F58A0:0xA4650000,  # clear halfword identity only
    0x30DD18:0x0C0BD650,  # follow initialization resolves route start
    0x30DD2C:0xA60004D0,  # passed count reset zero
    0x30DD38:0xA60504D4,  # initial direction +1
    0x30DD3C:0x0C0C3762,  # genuine dynamic GetNextPointOnRoute
    0x30DD48:0x0C0BD664,  # target copied through GetPointPosition
    0x30DDBC:0x2C4200C9,  # retail accepts index 200: unsafe boundary to contain
    0x30DE1C:0x2411FFFF,  # stop mode returns -1
    0x30DE2C:0x00031823,  # reverse mode negates direction
    0x30DE30:0xA60304D4,  # writes halfword direction
    0x30DE48:0x960304D4,  # restart mode retains direction
    0x30DE50:0xA60404D0,  # writes passed=-1
    0x30DE5C:0x00038C03,  # sign-extends result as a halfword
    0x30DE94:0x46040081,  # target.x minus ped.x
    0x30DE9C:0x46051841,  # target.y minus ped.y
    0x30DEC4:0x46000004,  # sqrt of XY squared distance
    0x30DEC8:0x460C0034,  # strictly less than arrival radius
    0x30DEF4:0x00641821,  # passed += direction
    0x30DEF8:0xA4A304D0,  # halfword passed state
    0x30A804:0x0C0C379E,  # actual FOLLOW_ROUTE branch checks arrival at radius 1
    0x30A81C:0x3C013F4C,  # high half of original 0.8 seek radius
    0x30A820:0x3421CCCD,  # low half of 0.8, no SA fixed-radius alias
    0x30B524:0x0C0790CE,  # actual shared call to SetSeek(vector, radius) at 1E4338
    0x1E4358:0x0C077B66,  # SetSeek calls IsPedInControl 1DED98
    0x1E4360:0x10400036,  # no state/destination write when not controllable
    0x1E43A8:0x50720025,  # FOLLOW_PATH refuses SetSeek
    0x1E4410:0xE614053C,  # radius is passed in, not guessed from movement
    0x1E4414:0xAE02034C,  # SEEK_POS state written before copied target
    0x1DED9C:0x28420027,  # original controllability requires state < 39
    0x1DEDB0:0x00031F78,  # source flag mask shifted to bits 43/44
    0x1DEDB8:0x14400008,  # either blocked-control flag returns false
    0x1DEDCC:0x46000836,  # health <= zero refuses control
    0x1E47C4:0x24110002,  # ordinary Seek chooses original WALK=2
    0x1E4824:0x00031FB8,  # running flag is bit 45
    0x1E4830:0x24110002,  # no running flag: retain WALK for route objective
    0x1E4980:0x00139FB8,  # movement override rechecks source running bit
    0x1E498C:0x24030004,  # source RUN=4, distinct from SA RUN=6
    0x1E4994:0x0062880B,  # when running, replace nextMove unless SPRINT
    0x308B70:0x2442FFCA,  # route setter: source dying/dead states 54/55
    0x308B74:0x2C420002,
    0x308B8C:0x5620002F,  # previous == new != NONE returns before route setup
    0x308BA8:0x860204CC,  # same-current route compares signed halfword id
    0x308BB4:0x860204D2,  # and signed halfword mode before early return
    0x308C00:0x0C0C2058,  # actual IsTemporaryObjective(308160)
    0x308C14:0xAE110254,  # initially write pending objective when temporary
    0x308C20:0x0C0C2032,  # ordinary SetStoredObjective(3080C8)
    0x308C40:0x0C0C373C,  # then unconditionally call SetFollowRoute(30DCF0)
    0x30DD30:0xAE030250,  # SetFollowRoute overwrites CURRENT objective with 30
    0x308160:0x24020010,  # exact temporary objective set:16,29,18,17,38
    0x308168:0x2402001D,
    0x308170:0x24020012,
    0x308178:0x24020011,
    0x308180:0x24020026,
    0x30836C:0x24020020,  # retail entity overload ALSO rejects state32
    0x308370:0x5062014A,  # absent in reconstruction, do not apply to route setter
    0x30838C:0x52300143,  # source entity cannot be self
    0x308394:0x8E020760,  # attached actor rejected outside combat objectives
    0x308464:0x8E020538,  # LEAVE requires genuine source in-vehicle field
    0x3084F8:0x0C0C2044,  # actual ForceStoredObjective for temporary setter
    0x309B40:0x8E030250,  # genuine RestorePreviousObjective starts with current
    0x309B44:0x10600022,  # NONE returns without inventing another assignment
    0x309B74:0x24020007,  # WAIT_IN_CAR_THEN_GET_OUT has a distinct engine path
    0x309B7C:0x8E020254,  # ordinary restore loads previous objective
    0x309BA0:0xAE000254,  # and consumes it
    0x309BA4:0xAE020250,  # write previous into current; not SA task disappearance
}
assert image.handler(486)['handler']==0x20BD64
assert image.handler(487)['handler']==0x20BDC0
assert image.handler(945)['handler']==0x2292F0
for address,word in expected.items():
    assert image.word(address)==word,(hex(address),hex(image.word(address)),hex(word))
# Independently pin both retail store tables, including the three entries
# that differ; the reconstruction's helper names are not sufficient evidence.
ordinary_skip={3,8,9,10,11,12,13,14,15,16,17,18,24,25,39,40,43,44,45,49,53}
enter_skip=ordinary_skip-{9,15,16}
for value in range(3,54):
    assert image.word(0x3C7110+(value-3)*4)==(0x308108 if value in ordinary_skip else 0x308100)
    assert image.word(0x3C71E0+(value-3)*4)==(0x308154 if value in enter_skip else 0x30814C)
for objective in (16,38):
    assert image.word(0x3C7390+(objective-7)*4)==0x308898  # same LEAVE always returns
for objective in (17,18):
    assert image.word(0x3C7390+(objective-7)*4)==0x3083F4  # same ENTER compares vehicle
print('PASS: original 200-node routes, halfword IDs, block-copy removal and dynamic follow consumer; runtimeSupportGranted=false')
