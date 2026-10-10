/* The factories for generators, environments and particle systems
 * (factory.cpp): each builds an object of the class a data file names. */

#pragma once

 
  void *  Gen_FactoryCreate(const char *name);
  void *  Env_FactoryCreate(const char *name);
  void *  PS_FactoryCreate(const char *name);
