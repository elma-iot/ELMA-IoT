import test from 'node:test';
import assert from 'node:assert/strict';
import {i2cAddresses,i2cCompatible,i2cIssues} from '../web/modules/i2c-policy.js';
import {occupiedPinChoices} from '../web/modules/peripheral-pin-policy.js';
test('OLED and BNO055 compatible; RTC and MPU6050 need distinct declared addresses',()=>{
 assert.ok(i2cCompatible(i2cAddresses('display:i2c-oled',60),i2cAddresses('sensor:bno055')));
 assert.ok(!i2cCompatible(i2cAddresses('sensor:ds3231-rtc'),i2cAddresses('sensor:mpu6050')));
 assert.ok(i2cCompatible(i2cAddresses('sensor:ds3231-rtc'),i2cAddresses('sensor:mpu6050',105)));
});
test('unknown modules cannot silently share; invalid addresses rejected',()=>{
 assert.ok(!i2cCompatible(i2cAddresses('display:custom'),[60]));
 assert.deepEqual(i2cAddresses('sensor:bno055',60),[]);
 assert.deepEqual(i2cAddresses('communication:i2c',128),[]);
});
test('PCA9685 reserves power-on All Call even with a declared address',()=>{
 assert.ok(!i2cCompatible(i2cAddresses('expansion:pca9685',64),[112]));
});
test('shared pair required, crossed lines rejected',()=>{
 const s={oled:{sdaPin:4,sclPin:5,i2cAddress:60},ui:{peripheralProfiles:{displayProfiles:['i2c-oled'],sensors:['bno055']},peripheralHelperBindings:{'sensor:0':{SDA:'4',SCL:'5'}}}};
 assert.deepEqual(i2cIssues(s),[]);
 s.ui.peripheralHelperBindings['sensor:0'].SCL='6';assert.ok(i2cIssues(s).length);
});
test('sharing exception does not permit non-I2C owners',()=>{
 const assignments=[{key:'oled.sdaPin',pin:4,label:'OLED SDA'},{key:'led',pin:4,label:'LED'}];
 assert.ok(occupiedPinChoices([4],assignments,'bno.sda','',a=>a.key==='oled.sdaPin')[0].disabled);
});

test('multiple PCA9685 chips may share All Call but not a unicast address',()=>{
 assert.ok(i2cCompatible(i2cAddresses('expansion:pca9685',64),i2cAddresses('expansion:pca9685',65),'expansion:pca9685','expansion:pca9685'));
 assert.ok(!i2cCompatible(i2cAddresses('expansion:pca9685',64),i2cAddresses('expansion:pca9685',64),'expansion:pca9685','expansion:pca9685'));
 assert.deepEqual(i2cAddresses('expansion:pca9685',112),[]);
});
